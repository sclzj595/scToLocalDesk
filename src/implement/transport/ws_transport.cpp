// ============================================================
// scToLocalDesk | src/implement/transport/ws_transport.cpp
// WsTransport 实现：Qt 信号 → C++ 回调桥接（Doc08 §7 线程模型）
// 发送走外箱队列：IO 线程入队、socket 线程 flush（QWebSocket 非线程安全）
// ============================================================
#include "implement/transport/ws_transport.hpp"

#include <QUrl>

#include "core/protocol/msg_type.hpp"

namespace sc::localdesk {

namespace {
constexpr int kFlushIntervalMs = 20;      ///< 发送外箱 flush 周期
constexpr int kFlushBudget = 32;          ///< 单次 flush 最多出队帧数（防饿主线程）
constexpr int kPingIntervalMs = 5000;     ///< KEEPALIVE ping 周期（Doc08 §6）
constexpr int kPingMaxMiss = 3;           ///< 连续无 pong 上限，超出判链路死亡
constexpr int kNonScreenWaitMs = 100;     ///< 非画面帧入队限时
}  // namespace

WsTransport::WsTransport(const QString& server_name)
    : server_name_(server_name) {
}

WsTransport::~WsTransport() {
    close();
}

bool WsTransport::start_server(uint16_t port) {
    server_ = std::make_unique<QWebSocketServer>(
        server_name_, QWebSocketServer::NonSecureMode);
    if (!server_->listen(QHostAddress::AnyIPv4, port)) {
        server_.reset();
        set_state(TransportState::Error);
        return false;  // 端口占用（Doc08 §6）
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
                         QObject::connect(sock, &QWebSocket::binaryMessageReceived,
                                          [this](const QByteArray& msg) {
                                              std::lock_guard<std::mutex> lk(cb_mtx_);
                                              if (on_message_) {
                                                  on_message_(std::vector<uint8_t>(
                                                      msg.cbegin(), msg.cend()));
                                              }
                                          });
                         QObject::connect(sock, &QWebSocket::disconnected, [this] {
                             peer_ = nullptr;
                             set_state(TransportState::Idle);
                         });
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
    peer_ = sock;
    ping_miss_ = 0;
    tx_.clear();
    tx_.reopen();
    QObject::connect(sock, &QWebSocket::connected, [this] {
        set_state(TransportState::Connected);
    });
    QObject::connect(sock, &QWebSocket::binaryMessageReceived,
                     [this](const QByteArray& msg) {
                         std::lock_guard<std::mutex> lk(cb_mtx_);
                         if (on_message_) {
                             on_message_(std::vector<uint8_t>(msg.cbegin(), msg.cend()));
                         }
                     });
    QObject::connect(sock, &QWebSocket::disconnected, [this] {
        peer_ = nullptr;
    });
    QObject::connect(sock, &QWebSocket::pong,
                     [this](quint64, const QByteArray&) { ping_miss_ = 0; });
    QObject::connect(sock, &QWebSocket::errorOccurred, [this] {
        set_state(TransportState::Error);
    });
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
    // 流控水位（Doc08 §4）：画面帧丢旧留新；其余帧限时等空位，超时丢弃
    const bool is_screen =
        data[0] == static_cast<uint8_t>(protocol::MsgType::ScreenFrame);
    if (is_screen) {
        (void)tx_.push_overwrite(std::move(ba));
    }
    else {
        (void)tx_.push_wait_for(std::move(ba), kNonScreenWaitMs);
    }
}

void WsTransport::close() {
    stop_timers();
    tx_.close();
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
    std::lock_guard<std::mutex> lk(cb_mtx_);
    if (on_state_) {
        on_state_(st);
    }
}

// ---------------- 发送外箱 / KEEPALIVE ----------------

void WsTransport::start_timers() {
    if (flush_timer_ == nullptr) {
        flush_timer_ = new QTimer();
        flush_timer_->setInterval(kFlushIntervalMs);
        QObject::connect(flush_timer_, &QTimer::timeout,
                         [this] { flush_outbox(); });
    }
    if (ping_timer_ == nullptr) {
        ping_timer_ = new QTimer();
        ping_timer_->setInterval(kPingIntervalMs);
        QObject::connect(ping_timer_, &QTimer::timeout, [this] { ping_tick(); });
    }
    flush_timer_->start();
    ping_timer_->start();
}

void WsTransport::stop_timers() {
    if (flush_timer_ != nullptr) {
        flush_timer_->stop();
        flush_timer_->deleteLater();
        flush_timer_ = nullptr;
    }
    if (ping_timer_ != nullptr) {
        ping_timer_->stop();
        ping_timer_->deleteLater();
        ping_timer_ = nullptr;
    }
}

void WsTransport::flush_outbox() {
    if (peer_ == nullptr) {
        return;
    }
    QByteArray pkt;
    int budget = kFlushBudget;
    while (budget-- > 0 && tx_.pop_wait(pkt, 0)) {
        if (peer_ == nullptr) {
            return;  // flush 期间对端断开
        }
        peer_->sendBinaryMessage(pkt);
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
    peer_->sendPing(QByteArrayLiteral("ka"));
    ++ping_miss_;
}

}  // namespace sc::localdesk
