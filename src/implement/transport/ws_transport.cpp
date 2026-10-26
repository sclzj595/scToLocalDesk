// ============================================================
// scToLocalDesk | src/implement/transport/ws_transport.cpp
// WsTransport 实现：Qt 信号 → C++ 回调桥接（Doc08 §7 线程模型）
// 发送走外箱队列：IO 线程入队、socket 线程事件驱动 drain（asio proactor，
// 无轮询定时器；空闲零延迟，高负载自动批并）
// ============================================================
#include "implement/transport/ws_transport.hpp"

#include <QNetworkProxy>
#include <QTimer>
#include <QUrl>

#include "core/protocol/msg_type.hpp"

namespace sc::localdesk {

namespace {
constexpr int kFlushBudget = 32;          ///< 单次 drain 最多出队帧数（防饿主线程）
// socket 写缓冲背压水位：QWebSocket::sendBinaryMessage 异步入 Qt 内部缓冲无上限，
// 1GB 文件压测实测发送端 PM 涨到 1.17GB（≈文件大小全堆在此）。超水位停写，
// 剩余由 bytesWritten 信号驱动续排（事件驱动，零忙等）
constexpr qint64 kSendWatermarkBytes = 4 * 1024 * 1024;
constexpr int kPingIntervalMs = 5000;     ///< KEEPALIVE ping 周期（Doc08 §6）
constexpr int kPingMaxMiss = 3;           ///< 连续无 pong 上限，超出判链路死亡
constexpr int kNonScreenWaitMs = 100;     ///< 非画面帧入队限时
}  // namespace

WsTransport::WsTransport(const QString& server_name)
    : server_name_(server_name) {
    // drain 上下文在构造线程创建（主线程），与 QWebSocket 线程亲和一致
    drain_ctx_ = std::make_unique<QObject>();
}

WsTransport::~WsTransport() {
    close();
    // drain_ctx_ 析构需在其亲和线程（主线程）；传输层在主线程析构 → 安全
    drain_ctx_.reset();
}

bool WsTransport::start_server(uint16_t port) {
    server_ = std::make_unique<QWebSocketServer>(
        server_name_, QWebSocketServer::NonSecureMode);
    if (!server_->listen(QHostAddress::AnyIPv4, port)) {
        last_error_ = server_->errorString();
        server_.reset();
        set_state(TransportState::Error);
        return false;  // 端口占用/权限等（Doc08 §6）
    }
    QObject::connect(server_.get(), &QWebSocketServer::newConnection,
                     [this] {
                         QWebSocket* sock = server_->nextPendingConnection();
                         if (peer_ != nullptr) {
                             peer_->close();
                             peer_->deleteLater();  // 单会话红线：新连接顶替旧连接
                         }
                         peer_ = sock;
                         ping_miss_ = 0;
                         tx_.clear();
                         tx_.reopen();
                         {
                             std::lock_guard<std::mutex> lk(cb_mtx_);
                             peer_addr_ = sock->peerAddress()
                                              .toString()
                                              .toStdString();
                         }
                         QObject::connect(sock, &QWebSocket::binaryMessageReceived,
                                          [this](const QByteArray& msg) {
                                              // asio 模式：锁内只拷回调，锁外分发——
                                              // 大帧处理不阻塞回调表的其他访问者
                                              // 零拷贝：一次堆分配直通订阅方（只读）
                                              OnMessageCb cb;
                                              {
                                                  std::lock_guard<std::mutex> lk(cb_mtx_);
                                                  cb = on_message_;
                                              }
                                              if (cb) {
                                                  cb(std::make_shared<const std::vector<uint8_t>>(
                                                      msg.cbegin(), msg.cend()));
                                              }
                                          });
                         QObject::connect(sock, &QWebSocket::disconnected, [this] {
                             peer_ = nullptr;
                             {
                                 std::lock_guard<std::mutex> lk(cb_mtx_);
                                 peer_addr_.clear();
                             }
                             set_state(TransportState::Idle);
                         });
                         // 写缓冲排空信号：背压停写后的续排驱动（防重原子合并）
                         QObject::connect(sock, &QWebSocket::bytesWritten,
                                          [this](qint64) { schedule_drain(); });
                         QObject::connect(sock, &QWebSocket::pong,
                                          [this](quint64, const QByteArray&) { ping_miss_ = 0; });
                         set_state(TransportState::Connected);
                     });
    start_timers();
    set_state(TransportState::Listening);
    return true;
}

void WsTransport::connect_client(const std::string& host, uint16_t port) {
    set_state(TransportState::Connecting);
    auto* sock = new QWebSocket();
    // 内网直连红线：禁走系统代理（clash 等会劫持 ws:// 拨号致 UnsupportedSocketOperation）
    sock->setProxy(QNetworkProxy::NoProxy);
    peer_ = sock;
    ping_miss_ = 0;
    tx_.clear();
    tx_.reopen();
    {
        std::lock_guard<std::mutex> lk(cb_mtx_);
        peer_addr_ = host;  // client 模式：对端即拨号目标
    }
    QObject::connect(sock, &QWebSocket::connected, [this] {
        set_state(TransportState::Connected);
    });
    QObject::connect(sock, &QWebSocket::binaryMessageReceived,
                     [this](const QByteArray& msg) {
                         // 锁内拷回调、锁外分发（同上，asio 模式）+ 零拷贝缓冲
                         OnMessageCb cb;
                         {
                             std::lock_guard<std::mutex> lk(cb_mtx_);
                             cb = on_message_;
                         }
                         if (cb) {
                             cb(std::make_shared<const std::vector<uint8_t>>(
                                 msg.cbegin(), msg.cend()));
                         }
                     });
    QObject::connect(sock, &QWebSocket::disconnected, [this] {
        peer_ = nullptr;
    });
    // 写缓冲排空信号：背压停写后的续排驱动（防重原子合并）
    QObject::connect(sock, &QWebSocket::bytesWritten,
                     [this](qint64) { schedule_drain(); });
    QObject::connect(sock, &QWebSocket::pong,
                     [this](quint64, const QByteArray&) { ping_miss_ = 0; });
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // Qt 6.2 无 errorOccurred（6.5 引入）；拨号失败由 disconnected 兜底
    QObject::connect(sock, &QWebSocket::errorOccurred, [this] {
        set_state(TransportState::Error);
    });
#endif
    start_timers();
    // WebSocket 天然消息边界，无粘包（TLV 帧对齐消息，Doc08 §8.4）
    sock->open(QUrl(QString("ws://%1:%2").arg(QString::fromStdString(host)).arg(port)));
}

void WsTransport::send_binary(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return;
    }
    QByteArray ba(reinterpret_cast<const char*>(data.data()),
                  static_cast<qsizetype>(data.size()));
    // 流控分队列（Doc08 §4 修订）：画面帧独立队列丢旧留新；文件/控制帧
    // 纯 FIFO 零丢弃——1GB 压测教训：混合队列 overwrite 挤掉文件片致跳号断传
    const bool is_screen =
        data[0] == static_cast<uint8_t>(protocol::MsgType::ScreenFrame);
    if (is_screen) {
        (void)screen_tx_.push_overwrite(std::move(ba));
    }
    else {
        // 零丢弃：满则重试直至入队或链路关闭（100ms/轮退避）
        while (!tx_.push_wait_for(QByteArray(ba), kNonScreenWaitMs)) {
            if (tx_.closed()) {
                return;
            }
        }
    }
    schedule_drain();   // proactor：入队即投递排空事件（原子防重）
}

void WsTransport::disconnect_peer() {
    if (peer_ != nullptr) {
        peer_->close();
        peer_->deleteLater();
        peer_ = nullptr;
        // peer 关闭触发 disconnected → set_state(Idle) → 会话层收尾
    }
}

void WsTransport::close() {
    stop_timers();
    tx_.close();
    screen_tx_.close();
    if (peer_ != nullptr) {
        peer_->close();
        peer_->deleteLater();
        peer_ = nullptr;
    }
    if (server_ != nullptr) {
        server_->close();
        server_.reset();
    }
    set_state(TransportState::Idle);
}

void WsTransport::set_message_callback(OnMessageCb cb) {
    std::lock_guard<std::mutex> lk(cb_mtx_);
    on_message_ = std::move(cb);
}

void WsTransport::set_state_callback(OnStateCb cb) {
    std::lock_guard<std::mutex> lk(cb_mtx_);
    on_state_ = std::move(cb);
}

void WsTransport::set_state(TransportState st) {
    state_ = st;
    // 锁内拷回调、锁外分发（回调可能级联会话层逻辑，勿持锁调用）
    OnStateCb cb;
    {
        std::lock_guard<std::mutex> lk(cb_mtx_);
        cb = on_state_;
    }
    if (cb) {
        cb(st);
    }
}

// ---------------- 发送外箱（事件驱动）/ KEEPALIVE ----------------

void WsTransport::schedule_drain() {
    // 原子防重：已有排空事件在途则不重复投递（drain 末尾有余量会自续）
    if (flush_scheduled_.exchange(true)) {
        return;
    }
    // 经 drain_ctx_（主线程亲和）把排空动作序列化到 socket 线程——
    // 等价 asio strand：同一时刻至多一个 drain 在执行
    QMetaObject::invokeMethod(
        drain_ctx_.get(),
        [this] {
            flush_scheduled_ = false;
            drain_outbox();
        },
        Qt::QueuedConnection);
}

void WsTransport::drain_outbox() {
    if (peer_ == nullptr) {
        return;
    }
    const auto defer_drain = [this] {
        QTimer::singleShot(10, drain_ctx_.get(), [this] {
            if (peer_ != nullptr && (!tx_.empty() || !screen_tx_.empty())) {
                drain_outbox();
            }
        });
    };
    QByteArray pkt;
    // 画面帧全发（队列容量 2，均为最新帧，保推流实时性）
    while (screen_tx_.pop_wait(pkt, 0)) {
        if (peer_ == nullptr) {
            return;
        }
        if (peer_->bytesToWrite() > kSendWatermarkBytes) {
            defer_drain();
            return;
        }
        peer_->sendBinaryMessage(pkt);
    }
    // 文件/控制帧按预算抽干（纯 FIFO，不丢）
    int budget = kFlushBudget;
    while (budget-- > 0) {
        if (peer_ == nullptr) {
            return;  // drain 期间对端断开
        }
        // 背压：socket 写缓冲超水位即停。停写后 10ms 退避自续排兜底——
        // 单靠 bytesWritten 续排存在丢失窗口（1GB 压测实测）
        if (peer_->bytesToWrite() > kSendWatermarkBytes) {
            defer_drain();
            return;
        }
        if (!tx_.pop_wait(pkt, 0)) {
            break;
        }
        peer_->sendBinaryMessage(pkt);
    }
    // 预算耗尽仍有积压：再投一轮（自续），保持批并节奏
    if (!tx_.empty() || !screen_tx_.empty()) {
        schedule_drain();
    }
}

void WsTransport::start_timers() {
    if (ping_timer_ == nullptr) {
        ping_timer_ = new QTimer();
        ping_timer_->setInterval(kPingIntervalMs);
        QObject::connect(ping_timer_, &QTimer::timeout, [this] { ping_tick(); });
    }
    ping_timer_->start();
}

void WsTransport::stop_timers() {
    if (ping_timer_ != nullptr) {
        ping_timer_->stop();
        ping_timer_->deleteLater();
        ping_timer_ = nullptr;
    }
}

void WsTransport::ping_tick() {
    if (peer_ == nullptr) {
        return;
    }
    if (ping_miss_ >= kPingMaxMiss) {
        // 链路死亡：主动断开，触发 disconnected → 会话层收尾
        peer_->close();
        return;
    }
    peer_->ping(QByteArrayLiteral("ka"));  // Qt6：sendPing 更名 ping
    ++ping_miss_;
}

}  // namespace sc::localdesk
