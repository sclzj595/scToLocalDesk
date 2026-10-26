// ============================================================
// scToLocalDesk | src/implement/transport/ws_transport.hpp
// ITransport 默认实现：封装 QWebSocketServer / QWebSocket（Doc08 §1）
// 仅 Qt 目标编入；Qt 依赖收敛于本文件（Doc02 §7.1 红线）
// ============================================================
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include <QObject>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>

#include "core/interface/itransport.hpp"
#include "foundation/bounded_queue.hpp"

namespace sc::localdesk {

class WsTransport final : public ITransport {
public:
    explicit WsTransport(const QString& server_name);
    ~WsTransport() override;

    // ---------------- ITransport ----------------

    bool start_server(uint16_t port) override;
    void connect_client(const std::string& host, uint16_t port) override;
    void send_binary(const std::vector<uint8_t>& data) override;
    void close() override;
    /// PRD P8：断会话连接保留监听（防僵尸连接；close_session 调用）
    void disconnect_peer() override;
    void set_message_callback(OnMessageCb cb) override;
    void set_state_callback(OnStateCb cb) override;

    /// start_server/connect_client 失败原因（Qt errorString，诊断端口占用类问题）
    std::string last_error() const override {
        return last_error_.toStdString();
    }

    /// 对端地址：server 模式=请求方 IP（确认弹窗展示），client 模式=拨号目标
    std::string peer_address() const override {
        std::lock_guard<std::mutex> lk(cb_mtx_);
        return peer_addr_;
    }

private:
    void set_state(TransportState st);
    void start_timers();   ///< KEEPALIVE（创建线程 = socket 所在线程）
    void stop_timers();
    /// 事件驱动 flush（asio proactor 模式）：入队时投递一次排空事件 →
    /// socket 线程抽干外箱 → 预算耗尽仍有余量则再投递；无定时器轮询，
    /// 空闲零延迟、高负载自动批并（Doc08 §4）
    void drain_outbox();
    /// 投递 drain 事件（原子防重：同一线程多次入队只投一次）
    void schedule_drain();
    void ping_tick();      ///< 链路层 KEEPALIVE：连续 3 次无 pong 断开（Doc08 §6）

    std::unique_ptr<QWebSocketServer> server_;  ///< 被控端监听
    QWebSocket* peer_{nullptr};                 ///< 当前会话对端（单会话，Doc08 §3）
    QString server_name_;
    QString last_error_;
    TransportState state_{TransportState::Idle};

    /// drain 事件投递上下文（纯 QObject，主线程亲和 = QWebSocket 线程；
    /// 传输层自身非 QObject，借它把排空动作序列化到 socket 线程——
    /// 等价 asio strand：同一时刻至多一个 drain 在执行）
    std::unique_ptr<QObject> drain_ctx_;

    // 发送外箱双队列（IO 线程入队，socket 线程事件驱动抽干）：
    // 文件/控制帧纯 FIFO 零丢弃（满则重试）；画面帧独立队列丢旧留新。
    // 1GB 压测教训：混合单队列时 screen 帧 push_overwrite 会挤掉队头文件片，
    // 接收端跳号断传——帧类别分队列后互不侵蚀
    foundation::BoundedQueue<QByteArray> tx_{128};
    foundation::BoundedQueue<QByteArray> screen_tx_{2};
    std::atomic<bool> flush_scheduled_{false};  ///< drain 事件防重标记
    QTimer* ping_timer_{nullptr};
    int ping_miss_{0};  ///< 连续无 pong 计数（仅 socket 线程访问）

    mutable std::mutex cb_mtx_;  ///< peer_address() const 亦加锁
    OnMessageCb on_message_;
    OnStateCb on_state_;
    std::string peer_addr_;  ///< 对端地址（cb_mtx_ 保护；断开即清）
};

}  // namespace sc::localdesk
