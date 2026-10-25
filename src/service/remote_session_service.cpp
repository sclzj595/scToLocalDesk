// ============================================================
// scToLocalDesk | src/service/remote_session_service.cpp
// 会话状态机实现：鉴权路由（人工/无人值守）+ 单会话红线 + 资源回收（Doc09）
// ============================================================
#include "service/remote_session_service.hpp"

#include <chrono>
#include <utility>

#include "core/auth/unattended_authenticator.hpp"
#include "core/events.hpp"
#include "core/interface/types.hpp"
#include "core/protocol/tlv.hpp"
#include "foundation/event_bus.hpp"
#include "foundation/logger/ilogger.hpp"
#include "service/alias_service.hpp"
#include "service/file_service.hpp"
#include "service/input_service.hpp"
#include "service/screen_stream_service.hpp"
#include "service/trust_store.hpp"

namespace sc::localdesk {

namespace {

using sc::localdesk::foundation::LogLevel;

int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int64_t monotonic_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string make_session_id() {
    static std::atomic<uint64_t> seq{0};
    return "sess-" + std::to_string(monotonic_ms()) + "-"
         + std::to_string(seq.fetch_add(1, std::memory_order_relaxed));
}

std::vector<uint8_t> hex_to_bytes(std::string_view hex) {
    std::vector<uint8_t> out;
    if (hex.size() % 2 != 0) {
        return out;
    }
    out.reserve(hex.size() / 2);
    const auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        const int hi = nib(hex[i]);
        const int lo = nib(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return {};
        }
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
}

}  // namespace

RemoteSessionService::RemoteSessionService(
    std::shared_ptr<ITransport> transport,
    std::shared_ptr<IAuthenticator> manual_auth,
    std::shared_ptr<UnattendedPasswordAuthenticator> unattended_auth,
    std::shared_ptr<ICrypto> crypto,
    std::shared_ptr<AliasService> aliases,
    std::shared_ptr<foundation::EventBus> bus,
    std::shared_ptr<foundation::ILogger> logger)
    : transport_(std::move(transport)),
      manual_auth_(std::move(manual_auth)),
      unattended_auth_(std::move(unattended_auth)),
      crypto_(std::move(crypto)),
      aliases_(std::move(aliases)),
      bus_(std::move(bus)),
      log_(std::move(logger)) {
}

RemoteSessionService::~RemoteSessionService() {
    close_session(CloseReason::Normal);
    // 看门狗收尾（PRD 确认三件套）：代次失效 → 唤醒 → 回收线程（不持锁 join）
    {
        std::lock_guard<std::mutex> lk(mtx_);
        ++confirm_seq_;
    }
    confirm_cv_.notify_all();
    if (confirm_watchdog_.joinable()) {
        confirm_watchdog_.join();
    }
}

void RemoteSessionService::set_file_service(std::shared_ptr<FileService> files) {
    files_ = std::move(files);
}

void RemoteSessionService::set_stream_service(std::shared_ptr<ScreenStreamService> stream) {
    stream_ = std::move(stream);
}

void RemoteSessionService::set_input_service(std::shared_ptr<InputService> input) {
    input_ = std::move(input);
}

void RemoteSessionService::set_stream_params(int fps, int quality) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (fps > 0) {
        stream_fps_ = fps;
    }
    if (quality > 0 && quality <= 100) {
        stream_quality_ = quality;
    }
}

bool RemoteSessionService::start_listening(uint16_t port) {
    if (transport_ == nullptr) {
        return false;
    }
    transport_->set_message_callback(
        [this](std::shared_ptr<const std::vector<uint8_t>> data) {
            on_transport_message(std::move(data));
        });
    transport_->set_state_callback(
        [this](TransportState st) { on_transport_state(st); });

    if (!transport_->start_server(port)) {
        SCLD_LOG(log_, LogLevel::Error, "session",
                 "端口 " + std::to_string(port) + " 监听失败："
                     + transport_->last_error());
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(mtx_);
        role_ = Role::Controlled;
        listening_ = true;
    }
    SCLD_LOG(log_, LogLevel::Info, "session",
             "被控监听已启动 ws_port=" + std::to_string(port));
    return true;
}

void RemoteSessionService::stop_listening() {
    if (transport_ != nullptr) {
        transport_->close();
    }
    listening_ = false;
    reset_to_idle();
}

bool RemoteSessionService::connect_to_peer(const std::string& alias, bool unattended,
                                           const std::string& password) {
    if (aliases_ == nullptr || transport_ == nullptr) {
        return false;
    }
    const auto peer = aliases_->resolve(alias);
    if (!peer.has_value()) {
        SCLD_LOG(log_, LogLevel::Warn, "session", "[alias=" + alias + "] 内网未找到该设备");
        return false;
    }
    // 自连拦截（PRD 连接黑名单）：目标 IP 属本机网卡集合 且 端口=本机监听端口
    // → 本地拒绝不发网络包；同机双实例（端口不同）/虚拟机均放行
    if (self_ip_checker_ && self_ip_checker_(peer->ip, peer->port)) {
        SCLD_LOG(log_, LogLevel::Warn, "session",
                 "[alias=" + alias + "] 目标 " + peer->ip + ":"
                     + std::to_string(peer->port) + " 为本机自身，拒绝连接");
        Evt_ConnectDenied e;
        e.device_alias = alias;
        e.reason = "不允许连接本机";
        bus_->publish(e);
        return false;
    }
    // 单会话红线：控制端同时只发起一路（Doc09 §3.4）
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (state_ != SessState::Idle) {
            return false;
        }
        role_ = Role::Controller;
        state_ = SessState::WaitForUser;  // 占位：防并发二次发起
        pending_connect_.alias = alias;
        pending_connect_.unattended = unattended;
        pending_connect_.password = password;
        pending_connect_.valid = true;
    }
    transport_->connect_client(peer->ip, peer->port);
    session_id_ = make_session_id();
    SCLD_LOG(log_, LogLevel::Info, "session",
             "[session_id=" + session_id_ + "][alias=" + alias + "] 发起协助 unattended="
                 + (unattended ? "1" : "0"));
    return true;
}

void RemoteSessionService::send_request_connect() {
    PendingConnect pc;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        pc = pending_connect_;
        pending_connect_ = {};
        if (pc.alias.empty()) {
            return;
        }
    }
    peer_alias_ = pc.alias;
    const uint8_t flags = pc.unattended ? protocol::connect_flags::kUnattended : 0x00;
    // 协议语义：REQUEST_CONNECT.body.alias = 发起方（本机）别名——被控端弹窗
    // 展示"谁在请求"与信任反查 peer_code 都依赖它；发目标别名会让被控端
    // 把自己当请求方（弹窗显示错名 + 信任直连永远反查失败）
    transport_->send_binary(
        protocol::build_request_connect(aliases_->self_alias(), flags));
    // 密码暂存到连接上下文：AUTH_CHALLENGE 到达时派生 proof 后立即清除
    if (pc.unattended && !pc.password.empty()) {
        std::lock_guard<std::mutex> lk(mtx_);
        pending_connect_.alias = pc.alias;
        pending_connect_.password = pc.password;  // 仅留密码供挑战应答
    }
}

void RemoteSessionService::approve() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (state_ != SessState::WaitForUser) {
        return;
    }
    ++confirm_seq_;            // 弹窗被消费，看门狗失效（PRD 确认三件套）
    confirm_cv_.notify_all();
    if (transport_ != nullptr) {
        transport_->send_binary(protocol::build_ctrl(protocol::CtrlSubType::Approved));
    }
    enter_running(false);
}

void RemoteSessionService::reject(RejectReason reason) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (state_ != SessState::WaitForUser && state_ != SessState::WaitForUnattended) {
        return;
    }
    ++confirm_seq_;
    confirm_cv_.notify_all();
    if (transport_ != nullptr) {
        transport_->send_binary(protocol::build_reject(reason));
    }
    reset_to_idle();
}

void RemoteSessionService::close_session(CloseReason reason) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (state_ == SessState::Idle) {
            return;
        }
        state_ = SessState::Closing;
    }
    if (transport_ != nullptr) {
        transport_->send_binary(protocol::build_close(reason));
    }
    reset_to_idle();
    SCLD_LOG(log_, LogLevel::Info, "session",
             "会话关闭 reason=" + std::to_string(static_cast<int>(reason)));
}

bool RemoteSessionService::send_business_frame(const std::vector<uint8_t>& frame) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (state_ != SessState::Running) {  // 鉴权红线：Running 前不发业务帧
        return false;
    }
    if (transport_ != nullptr) {
        transport_->send_binary(frame);
        return true;
    }
    return false;
}

bool RemoteSessionService::send_input_event(const protocol::MouseKeyBody& mk) {
    return send_business_frame(protocol::build_mouse_key(mk));
}

RemoteSessionService::SessState RemoteSessionService::state() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return state_;
}

std::string RemoteSessionService::session_id() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return session_id_;
}

// ---------------- Transport 回调 ----------------

void RemoteSessionService::on_transport_message(
    std::shared_ptr<const std::vector<uint8_t>> data) {
    uint32_t body_len = 0;
    const auto type = protocol::parse_header(data->data(), data->size(), body_len);
    if (!type.has_value() || data->size() < protocol::kHeaderSize + body_len) {
        return;  // 畸形报文丢弃
    }
    const uint8_t* body = data->data() + protocol::kHeaderSize;
    switch (*type) {
    case protocol::MsgType::SessionCtrl:
        handle_session_ctrl(body, body_len);
        break;
    case protocol::MsgType::FileMeta:
    case protocol::MsgType::FileChunk:
    case protocol::MsgType::MouseKey:
    case protocol::MsgType::ScreenFrame: {
        std::lock_guard<std::mutex> lk(mtx_);
        if (state_ != SessState::Running) {
            // 未鉴权业务报文一律丢弃（Doc14 §2.8 安全用例）
            SCLD_LOG(log_, LogLevel::Warn, "session",
                     "[session_id=" + session_id_ + "] 未鉴权业务报文，已丢弃 type="
                         + std::to_string(static_cast<int>(*type)));
            return;
        }
    } break;
    default:
        return;
    }

    // Running 态分派（此时已释放锁）
    switch (*type) {
    case protocol::MsgType::FileMeta:
        if (files_ != nullptr) {
            files_->on_file_meta(body, body_len);
        }
        break;
    case protocol::MsgType::FileChunk:
        if (files_ != nullptr) {
            (void)files_->on_file_chunk(body, body_len);
        }
        break;
    case protocol::MsgType::MouseKey:
        // 被控端：注入键鼠（控制端收到即丢弃——理论上不会发）
        if (input_ != nullptr) {
            (void)input_->handle(body, body_len);
        }
        break;
    case protocol::MsgType::ScreenFrame: {
        // 控制端：帧头解析后经事件零拷贝上抛（解码渲染在 platform 层，Doc18 §4.2）
        protocol::ScreenFrameHeader hdr;
        const uint8_t* px = nullptr;
        size_t px_len = 0;
        if (!protocol::parse_screen_frame(body, body_len, hdr, px, px_len)
            || px == nullptr || px_len == 0) {
            break;  // 畸形帧丢弃
        }
        if (bus_ != nullptr) {
            Evt_VideoFrame e;
            e.payload = std::make_shared<std::vector<uint8_t>>(px, px + px_len);
            e.width = hdr.width;
            e.height = hdr.height;
            e.timestamp_us = hdr.timestamp_us;
            bus_->publish(e);
        }
        break;
    }
    default:
        break;
    }
}

void RemoteSessionService::on_transport_state(TransportState st) {
    if (st == TransportState::Connected) {
        // 控制端：连接建立 → 发送 REQUEST_CONNECT（Doc05 §4.1）
        send_request_connect();
    }
    else if (st == TransportState::Error) {
        if (bus_ != nullptr) {
            Evt_SessionClosed e;
            e.session_id = session_id_;
            e.reason = SessionEndReason::NetworkError;
            e.timestamp_us = now_us();
            bus_->publish(e);
        }
        close_session(CloseReason::Error);
    }
    else if (st == TransportState::Idle) {
        // 对端断开
        if (state() == SessState::Running) {
            if (bus_ != nullptr) {
                Evt_SessionClosed e;
                e.session_id = session_id_;
                e.reason = SessionEndReason::PeerQuit;
                e.timestamp_us = now_us();
                bus_->publish(e);
            }
            close_session(CloseReason::Normal);
        }
    }
}

// ---------------- SESSION_CTRL 处理 ----------------

void RemoteSessionService::handle_session_ctrl(const uint8_t* body, size_t len) {
    if (body == nullptr || len == 0) {
        return;
    }
    const auto sub = static_cast<protocol::CtrlSubType>(body[0]);
    switch (sub) {
    case protocol::CtrlSubType::RequestConnect: {
        protocol::RequestConnectBody req;
        if (!protocol::parse_request_connect(body, len, req)) {
            SCLD_LOG(log_, LogLevel::Warn, "session", "REQUEST_CONNECT 解析失败，丢弃");
            return;
        }
        handle_request_connect(req);
        break;
    }
    case protocol::CtrlSubType::AuthChallenge:
        handle_auth_challenge(body + 1, len - 1);
        break;
    case protocol::CtrlSubType::AuthProof:
        handle_auth_proof(body + 1, len - 1);
        break;
    case protocol::CtrlSubType::Approved: {
        // 控制端收到同意：进入 Running
        std::lock_guard<std::mutex> lk(mtx_);
        enter_running(false);
        break;
    }
    case protocol::CtrlSubType::Reject:
        // 控制端收到拒绝：按 reason 提示（Doc13 §3.3）；拒绝属审计日志（Warn 即时落盘）
        SCLD_LOG(log_, LogLevel::Warn, "session",
                 "对端拒绝 reason=" + std::to_string(static_cast<int>(len > 1 ? body[1] : 0)));
        {
            std::lock_guard<std::mutex> lk(mtx_);
            reset_to_idle();
        }
        break;
    case protocol::CtrlSubType::Close:
        close_session(CloseReason::Normal);  // 对端主动断开（Doc09 §5）
        break;
    case protocol::CtrlSubType::Keepalive:
    default:
        break;
    }
}

void RemoteSessionService::handle_request_connect(const protocol::RequestConnectBody& req) {
    // 单会话红线：占用中直接 REJECT(0x02)（Doc09 §3.4）
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (state_ != SessState::Idle) {
            if (transport_ != nullptr) {
                transport_->send_binary(
                    protocol::build_reject(protocol::RejectReason::SessionBusy));
            }
            SCLD_LOG(log_, LogLevel::Warn, "session",
                     "[alias=" + req.alias + "] 会话占用，拒绝新请求");
            return;
        }
    }

    session_id_ = make_session_id();
    peer_alias_ = req.alias;

    // 信任缓存直连（Doc18 §3.2）：对端连接码已记忆 → 免人工确认直接放行。
    // alias→code 反查走在线表；对端未上报 code（旧心跳）或不在表内则
    // 自然降级人工确认——信任判定永远以本机 trust.json 为准，不扩协议
    if (trust_ != nullptr) {
        uint32_t peer_code = 0;
        for (const auto& dev : aliases_->online_devices()) {
            if (dev.alias == req.alias) {
                peer_code = dev.code;
                break;
            }
        }
        if (peer_code != 0 && trust_->is_trusted(std::to_string(peer_code))) {
            std::lock_guard<std::mutex> lk(mtx_);
            if (transport_ != nullptr) {
                transport_->send_binary(
                    protocol::build_ctrl(protocol::CtrlSubType::Approved));
            }
            enter_running(false);
            publish_request_event(req.alias, true);
            SCLD_LOG(log_, LogLevel::Info, "session",
                     "[session_id=" + session_id_ + "][alias=" + req.alias
                         + "] 已信任设备，自动放行 code=" + std::to_string(peer_code));
            return;
        }
    }

    // 鉴权路由（Doc09 §3.5 / Doc16 §3）
    AuthRequest auth_req;
    auth_req.peer_alias = req.alias;
    auth_req.unattended = (req.flags & protocol::connect_flags::kUnattended) != 0;

    const auto result = unattended_auth_ != nullptr
                            ? unattended_auth_->decide(auth_req)
                            : manual_auth_->decide(auth_req);

    switch (result) {
    case AuthResult::NeedManualConfirm: {
        uint64_t my_seq = 0;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            state_ = SessState::WaitForUser;
            my_seq = ++confirm_seq_;  // 新弹窗代次（PRD 确认三件套）
            publish_request_event(req.alias, false);
            SCLD_LOG(log_, LogLevel::Info, "session",
                     "[session_id=" + session_id_ + "][alias=" + req.alias
                         + "] 连接请求，等待人工确认");
        }
        arm_confirm_watchdog(my_seq);  // 30s 无响应自动拒（锁外武装，内部 join）
        break;
    }
    case AuthResult::StartChallenge: {
        // 被控端：salt(本机凭据) + challenge(一次性) 下发（Doc05 §6）
        const std::string challenge = unattended_auth_->issue_challenge();
        const std::string salt_hex = unattended_auth_->secret().salt_hex;
        const auto chal = hex_to_bytes(challenge);
        const auto salt = hex_to_bytes(salt_hex);
        if (chal.size() != 16 || salt.size() != 16) {
            std::lock_guard<std::mutex> lk(mtx_);
            reset_to_idle();
            if (transport_ != nullptr) {
                transport_->send_binary(protocol::build_reject(protocol::RejectReason::Other));
            }
            return;
        }
        {
            std::lock_guard<std::mutex> lk(mtx_);
            pending_challenge_ = challenge;
            state_ = SessState::WaitForUnattended;
        }
        transport_->send_binary(protocol::build_auth_challenge(salt.data(), chal.data()));
        SCLD_LOG(log_, LogLevel::Info, "session",
                 "[session_id=" + session_id_ + "][alias=" + req.alias
                     + "] 无人值守挑战已下发");
        break;
    }
    case AuthResult::Rejected:  // 无人值守开关关闭（Doc16 §3）
    default:
        if (transport_ != nullptr) {
            transport_->send_binary(protocol::build_reject(protocol::RejectReason::Other));
        }
        SCLD_LOG(log_, LogLevel::Info, "session",
                 "[alias=" + req.alias + "] 无人值守开关未开启，拒绝");
        break;
    }
}

void RemoteSessionService::handle_auth_challenge(const uint8_t* body, size_t len) {
    // 控制端：salt(16B)+challenge(16B) → K=PBKDF2(password,salt) → proof=HMAC(challenge,K)
    if (body == nullptr || len < 32 || crypto_ == nullptr) {
        return;
    }
    std::string password;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (state_ == SessState::Idle) {
            return;
        }
        password = pending_connect_.password;
        pending_connect_.password.clear();  // 瞬态：用后即清（Doc16 §2.4）
    }
    if (password.empty()) {
        return;
    }
    // TODO(V1.0): PBKDF2 10 万迭代移交线程池，避免阻塞 IO 线程（Doc12 §3）
    std::vector<uint8_t> key;
    if (!crypto_->derive_key(password,
                             std::string_view(reinterpret_cast<const char*>(body), 16),
                             key)) {
        SCLD_LOG(log_, LogLevel::Error, "session", "无人值守口令派生失败");
        return;
    }
    std::vector<uint8_t> proof;
    if (!crypto_->hmac_sha256(
            std::string_view(reinterpret_cast<const char*>(key.data()), key.size()),
            std::string_view(reinterpret_cast<const char*>(body + 16), 16), proof)) {
        return;
    }
    if (transport_ != nullptr) {
        transport_->send_binary(protocol::build_auth_proof(proof.data()));
    }
}

void RemoteSessionService::handle_auth_proof(const uint8_t* body, size_t len) {
    if (unattended_auth_ == nullptr) {
        return;
    }
    std::string challenge;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (state_ != SessState::WaitForUnattended) {
            return;
        }
        challenge = pending_challenge_;
    }
    const std::string proof(reinterpret_cast<const char*>(body), len);
    const bool ok = unattended_auth_->verify_proof(challenge, proof);

    if (transport_ != nullptr) {
        if (ok) {
            transport_->send_binary(protocol::build_ctrl(protocol::CtrlSubType::Approved));
        }
        else {
            transport_->send_binary(
                protocol::build_reject(protocol::RejectReason::PasswordWrong));
        }
    }
    std::lock_guard<std::mutex> lk(mtx_);
    if (ok) {
        enter_running(true);
    }
    else {
        reset_to_idle();
    }
}

// ---------------- 状态迁移 ----------------

void RemoteSessionService::enter_running(bool unattended) {
    // 前置：调用方已持锁且 state_ 已迁移
    state_ = SessState::Running;
    // 文件事件来源名（Evt_FileMetaReceived.device_alias 弹窗展示，Doc18 §5）
    if (files_ != nullptr) {
        files_->set_peer_alias(peer_alias_);
    }
    // 被控端 Running 即开推流（Doc09 §3.1 鉴权红线：APPROVED 之后才推）
    // start 非阻塞（内部起线程），持锁调用安全
    if (role_ == Role::Controlled && stream_ != nullptr) {
        const int fps = stream_fps_;
        const int quality = stream_quality_;
        stream_->start([this](const std::vector<uint8_t>& f) { return send_business_frame(f); },
                       ScreenStreamService::Options{fps, quality});
    }
    const int64_t ts = now_us();
    if (bus_ != nullptr) {
        Evt_SessionStart e;
        e.session_id = session_id_;
        e.device_alias = peer_alias_;
        e.unattended = unattended;
        e.timestamp_us = ts;
        bus_->publish(e);
    }
    SCLD_LOG(log_, LogLevel::Info, "session",
             "[session_id=" + session_id_ + "][alias=" + peer_alias_
                 + "] 会话建立 unattended=" + (unattended ? "1" : "0"));
    // 无人值守自动同意：额外发审计事件（Doc16 §2.5）
    if (unattended && bus_ != nullptr) {
        Evt_UnattendedLogin u;
        u.session_id = session_id_;
        u.device_alias = peer_alias_;
        u.started_at_us = ts;
        bus_->publish(u);
    }
}

void RemoteSessionService::reset_to_idle() {
    // 资源回收红线（Doc09 §5）：停推流/停文件管线。
    // stop() 仅置位+关队列（非阻塞），持锁/不持锁调用均安全
    if (stream_ != nullptr) {
        stream_->stop();
    }
    if (files_ != nullptr) {
        files_->stop_all();
        files_->set_peer_alias("");  // 清来源名，防下次会话前残留
    }
    state_ = SessState::Idle;
    pending_challenge_.clear();
    peer_alias_.clear();
    pending_connect_ = {};
    session_id_.clear();
}

void RemoteSessionService::publish_request_event(const std::string& peer, bool auto_approved) {
    if (bus_ == nullptr) {
        return;
    }
    Evt_ConnectRequest e;
    e.session_id = session_id_;
    e.device_alias = peer;
    e.auto_approved = auto_approved;
    // 请求方 IP（PRD 确认三件套）：REQUEST_CONNECT 到达时的对端 TCP 地址
    e.peer_ip = transport_ != nullptr ? transport_->peer_address() : "";
    e.timestamp_us = now_us();
    bus_->publish(e);
}

void RemoteSessionService::arm_confirm_watchdog(uint64_t my_seq) {
    // 前置：调用方不持锁。旧代次线程 pred 已因 seq 变更不成立，唤醒即退
    confirm_cv_.notify_all();
    if (confirm_watchdog_.joinable()) {
        confirm_watchdog_.join();
    }
    confirm_watchdog_ = std::thread([this, my_seq] {
        std::unique_lock<std::mutex> lk(mtx_);
        // 等待期间弹窗被消费（同意/拒绝/断开/新请求）→ pred 为真提前醒
        const bool consumed = confirm_cv_.wait_for(
            lk, std::chrono::seconds(kConfirmTimeoutSec),
            [this, my_seq] { return confirm_seq_ != my_seq; });
        if (consumed || state_ != SessState::WaitForUser) {
            return;
        }
        // PRD 确认三件套：30s 无响应自动拒绝，对端收 REJECT(Timeout)
        if (transport_ != nullptr) {
            transport_->send_binary(
                protocol::build_reject(protocol::RejectReason::Timeout));
        }
        reset_to_idle();  // 持锁调用安全（stop 均非阻塞）
        SCLD_LOG(log_, LogLevel::Info, "session",
                 "确认弹窗 " + std::to_string(kConfirmTimeoutSec)
                     + "s 未响应，已自动拒绝");
        if (bus_ != nullptr) {
            Evt_ConnectRequestTimeout e;
            e.timestamp_us = now_us();
            bus_->publish(e);  // UI 关弹窗 + 提示
        }
    });
}

}  // namespace sc::localdesk
