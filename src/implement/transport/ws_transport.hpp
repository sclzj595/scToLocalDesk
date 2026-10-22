// ============================================================
// scToLocalDesk | src/implement/transport/ws_transport.hpp
// ITransport 默认实现：封装 QWebSocketServer / QWebSocket（Doc08 §1）
// 仅 Qt 目标编入；Qt 依赖收敛于本文件（Doc02 §7.1 红线）
// ============================================================
#pragma once

#include <memory>
#include <mutex>
#include <vector>

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

    [[nodiscard]] bool start_server(uint16_t port) override;
    void connect_client(const std::string& host, uint16_t port) override;
    void send_binary(const std::vector<uint8_t>& data) override;
    void close() override;
    void set_message_callback(OnMessageCb cb) override;
    void set_state_callback(OnStateCb cb) override;

private:
    void set_state(TransportState st);
    void start_timers();   ///< flush + KEEPALIVE（创建线程 = socket 所在线程）
    void stop_timers();
    void flush_outbox();   ///< 发送外箱批量出队（20ms tick，Doc08 §4）
    void ping_tick();      ///< 链路层 KEEPALIVE：连续 3 次无 pong 断开（Doc08 §6）

    std::unique_ptr<QWebSocketServer> server_;  ///< 被控端监听
    QWebSocket* peer_{nullptr};                 ///< 当前会话对端（单会话，Doc08 §3）
    QString server_name_;
    TransportState state_{TransportState::Idle};

    // 发送外箱：IO 线程入队（QWebSocket 非线程安全，禁止跨线程直发），
    // socket 线程 20ms 批量 flush。画面帧 push_overwrite 丢旧留新，其余 push_wait_for
    foundation::BoundedQueue<QByteArray> tx_{128};
    QTimer* flush_timer_{nullptr};
    QTimer* ping_timer_{nullptr};
    int ping_miss_{0};  ///< 连续无 pong 计数（仅 socket 线程访问）

    std::mutex cb_mtx_;
    OnMessageCb on_message_;
    OnStateCb on_state_;
};

}  // namespace sc::localdesk
