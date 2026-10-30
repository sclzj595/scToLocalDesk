// ============================================================
// scToLocalDesk | src/service/remote_session_service.cpp
// 会话状态机实现：鉴权路由（人工/无人值守）+ 单会话红线 + 资源回收（Doc09）
// ============================================================
#include "service/remote_session_service.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>
#include <utility>

#include "core/auth/unattended_authenticator.hpp"
#include "core/events.hpp"
#include "core/interface/types.hpp"
#include "core/protocol/tlv.hpp"
#include "foundation/event_bus.hpp"
#include "foundation/logger/audit_logger.hpp"
#include "foundation/logger/ilogger.hpp"
#include "service/alias_service.hpp"
#include "service/blacklist_store.hpp"
#include "service/file_service.hpp"
#include "service/input_service.hpp"
#include "service/screen_stream_service.hpp"
#include "service/session_crypto.hpp"
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

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
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
    // 文件出站绑加密链路（PRD §6.2 全载荷加密）：send_business_frame 内部
    // 按 encrypted_ 状态自动选择明文/密文，密钥协商后文件帧不再裸发
    if (files_ != nullptr) {
        files_->set_sender(
            [this](const std::vector<uint8_t>& f) { return send_business_frame(f); });
    }
}

void RemoteSessionService::set_stream_service(std::shared_ptr<ScreenStreamService> stream) {
    stream_ = std::move(stream);
}

void RemoteSessionService::set_input_service(std::shared_ptr<InputService> input) {
    input_ = std::move(input);
    // 只读丢弃日志需 logger：装配期本服务 log_ 已就绪，顺手注入省装配改动
    if (input_ != nullptr) {
        input_->set_logger(log_.get());
    }
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

void RemoteSessionService::cancel_connect() {
    // PRD P7 取消按钮：拨号/等确认期用户主动放弃（Running 会话走断开不走此）
    if (state() == SessState::Running) {
        return;
    }
    SCLD_LOG(log_, LogLevel::Info, "session", "控制端取消连接");
    close_session(CloseReason::Normal);
}

bool RemoteSessionService::can_start_control() const {
    return state() == SessState::Idle;  // 单会话红线（Doc09 §3.4）
}

bool RemoteSessionService::connect_to_peer(const std::string& alias, bool unattended,
                                           const std::string& password) {
    if (aliases_ == nullptr || transport_ == nullptr) {
        return false;
    }
    DeviceInfo peer;
    if (!aliases_->resolve(alias, peer)) {
        SCLD_LOG(log_, LogLevel::Warn, "session", "[alias=" + alias + "] 内网未找到该设备");
        return false;
    }
    // 自连拦截二道防线（别名口径，先于 IP 判定）：连接名=本机（如信任缓存
    // 存了自己的历史垃圾条目）直接拒绝，不依赖 IP/端口推导
    if (alias == aliases_->self_alias()) {
        SCLD_LOG(log_, LogLevel::Warn, "session",
                 "[alias=" + alias + "] 目标为本机别名，拒绝连接");
        Evt_ConnectDenied e;
        e.device_alias = alias;
        e.reason = "不允许连接本机";
        bus_->publish(e);
        return false;
    }
    // 自连拦截（PRD 连接黑名单）：目标 IP 属本机网卡集合 且 端口=本机监听端口
    // → 本地拒绝不发网络包；同机双实例（端口不同）/虚拟机均放行
    if (self_ip_checker_ && self_ip_checker_(peer.ip, peer.port)) {
        SCLD_LOG(log_, LogLevel::Warn, "session",
                 "[alias=" + alias + "] 目标 " + peer.ip + ":"
                     + std::to_string(peer.port) + " 为本机自身，拒绝连接");
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
    transport_->connect_client(peer.ip, peer.port);
    session_id_ = make_session_id();
    last_peer_ = alias;  // 重连按钮数据源（PRD P7）
    SCLD_LOG(log_, LogLevel::Info, "session",
             "[session_id=" + session_id_ + "][alias=" + alias + "] 发起协助 unattended="
                 + (unattended ? "1" : "0"));
    if (bus_ != nullptr) {
        Evt_ConnectStateChanged e;
        e.stage = "connecting";
        e.alias = alias;
        e.timestamp_us = now_us();
        bus_->publish(e);
    }
    // 出站看门狗：拨号失败/对端忙/REJECT 丢失 → 35s 复位 Idle，
    // 根治「明明没连接但一直提示有会话」状态卡死（双机实战踩坑）。
    // 换代次须在锁内，join 旧线程须在锁外（旧线程退出前要抢锁查 pred，
    // 持锁 join = 等自己放锁，死锁）
    uint64_t my_seq = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        ++confirm_seq_;  // 作废旧代次看门狗
        my_seq = confirm_seq_;
    }
    arm_outbound_watchdog(my_seq);
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
    // PRD P7 状态流转：REQUEST_CONNECT 已发 → 控制端"等待对方确认"（取消可退出）
    if (role_ == Role::Controller && bus_ != nullptr) {
        Evt_ConnectStateChanged e;
        e.stage = "wait_confirm";
        e.alias = pc.alias;
        e.timestamp_us = now_us();
        bus_->publish(e);
    }
    // 协议语义：REQUEST_CONNECT.body.alias = 发起方（本机）别名——被控端弹窗
    // 展示"谁在请求"与信任反查 peer_code 都依赖它；发目标别名会让被控端
    // 把自己当请求方（弹窗显示错名 + 信任直连永远反查失败）
    transport_->send_binary(protocol::build_request_connect(
        aliases_->self_alias(), flags, protocol::kProtoVersion));
    // 密码暂存到连接上下文：AUTH_CHALLENGE 到达时派生 proof 后立即清除
    if (pc.unattended && !pc.password.empty()) {
        std::lock_guard<std::mutex> lk(mtx_);
        pending_connect_.alias = pc.alias;
        pending_connect_.password = pc.password;  // 仅留密码供挑战应答
    }
}

void RemoteSessionService::approve(bool read_only) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (state_ != SessState::WaitForUser) {
        return;
    }
    ++confirm_seq_;            // 弹窗被消费，看门狗失效（PRD 确认三件套）
    confirm_cv_.notify_all();
    // 只读告知（P1）：Approved 尾随 1B（0=可控制 1=只读），控制端据此显示
    // 只读徽标；旧控制端按单字节 Approved 解析、多余字节忽略——双向兼容
    if (transport_ != nullptr) {
        transport_->send_binary(
            protocol::build_ctrl(protocol::CtrlSubType::Approved, read_only ? 1 : 0));
    }
    // 审批审计（Doc19 §6）：同意即放行键鼠/画面，追溯刚需；只读审批单独标注
    audit_event("Approve", "OK", peer_code_str_, peer_ip_,
                read_only ? "人工确认同意（只读模式）" : "人工确认同意");
    read_only_ = read_only;  // enter_running 推给 input_（Running 期丢弃键鼠）
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
    // 拒绝审计（含看门狗超时自动拒）：超时 reason=Timeout 与手动拒绝区分
    audit_event("Reject",
                reason == RejectReason::Timeout ? "Timeout" : "Denied",
                peer_code_str_, peer_ip_,
                reason == RejectReason::Timeout ? "看门狗超时自动拒" : "人工拒绝");
    reset_to_idle();
}

void RemoteSessionService::close_session(CloseReason reason,
                                         SessionEndReason end_reason) {
    std::string sid;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (state_ == SessState::Idle) {
            return;
        }
        state_ = SessState::Closing;
        sid = session_id_;  // reset_to_idle 会清，事件先拷
        // 会话结束审计（Doc19 §6）：reset_to_idle 清缓存前取列值
        audit_event("SessionEnd", "OK", peer_code_str_, peer_ip_,
                    "close_reason=" + std::to_string(static_cast<int>(reason)));
    }
    if (transport_ != nullptr) {
        transport_->send_binary(protocol::build_close(reason));
        transport_->disconnect_peer();  // PRD P8：断链防僵尸连接（server 监听保留）
    }
    reset_to_idle();
    // 审计类事件 Warn 即时落盘（spdlog flush_on(Warn)，Info 强杀丢缓冲）
    SCLD_LOG(log_, LogLevel::Warn, "session",
             "会话关闭 reason=" + std::to_string(static_cast<int>(reason)));
    // Evt_SessionClosed 唯一发布点（PRD P8）：主动断开/对端断开/网络错误
    // 全部经此，UI（连接条收起/页面切回/托盘复位）单一消费
    if (bus_ != nullptr) {
        Evt_SessionClosed e;
        e.session_id = sid;
        e.reason = end_reason;
        e.timestamp_us = now_us();
        bus_->publish(e);
    }
}

bool RemoteSessionService::send_business_frame(const std::vector<uint8_t>& frame) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (state_ != SessState::Running) {  // 鉴权红线：Running 前不发业务帧
        return false;
    }
    if (transport_ == nullptr) {
        return false;
    }
    if (encrypted_.load(std::memory_order_relaxed)) {
        // 会话加密（PRD）：fail-closed——加密失败宁丢帧不回退明文
        std::vector<uint8_t> cipher;
        if (crypto_ == nullptr || !crypto_->encrypt(frame, cipher)) {
            SCLD_LOG(log_, LogLevel::Error, "session",
                     "[session_id=" + session_id_ + "] 业务帧加密失败，拒发明文");
            return false;
        }
        transport_->send_binary(cipher);
        return true;
    }
    transport_->send_binary(frame);
    return true;
}

bool RemoteSessionService::send_input_event(const protocol::MouseKeyBody& mk) {
    return send_business_frame(protocol::build_mouse_key(mk));
}

bool RemoteSessionService::send_clipboard(const std::string& text) {
    if (text.empty() || text.size() > protocol::kClipboardMaxBytes) {
        return false;
    }
    const bool ok = send_business_frame(protocol::build_clipboard_sync(text));
    if (ok) {
        audit_clipboard_once();  // 会话内首次同步记一条审计（去重）
    }
    return ok;
}

void RemoteSessionService::audit_clipboard_once() {
    if (clipboard_audit_done_.exchange(true)) {
        return;  // 已记过：后续同步不刷屏
    }
    // 对端审计列持锁快照（本方法可能运行于收帧线程）
    std::string code, ip;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        code = peer_code_str_;
        ip = peer_ip_;
    }
    audit_event("ClipboardSync", "OK", code, ip, "剪贴板同步开启");
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
    // 会话加密（PRD）：协商激活后入站报文一律先解密，
    // fail-closed——解密失败（密钥不一致/被篡改）整帧丢弃
    if (encrypted_.load(std::memory_order_relaxed)) {
        auto plain = std::make_shared<std::vector<uint8_t>>();
        if (crypto_ == nullptr || !crypto_->decrypt(*data, *plain)) {
            SCLD_LOG(log_, LogLevel::Warn, "session",
                     "[session_id=" + session_id_ + "] 报文解密失败，已丢弃");
            return;
        }
        data = plain;
    }
    uint32_t body_len = 0;
    protocol::MsgType type;
    if (!protocol::parse_header(data->data(), data->size(), body_len, type)
        || data->size() < protocol::kHeaderSize + body_len) {
        return;  // 畸形报文丢弃
    }
    const uint8_t* body = data->data() + protocol::kHeaderSize;
    switch (type) {
    case protocol::MsgType::SessionCtrl:
        handle_session_ctrl(body, body_len);
        break;
    case protocol::MsgType::KeySync:
        handle_key_sync(body, body_len);
        break;
    case protocol::MsgType::FileMeta:
    case protocol::MsgType::FileChunk:
    case protocol::MsgType::MouseKey:
    case protocol::MsgType::ClipboardSync:
    case protocol::MsgType::ScreenFrame: {
        std::lock_guard<std::mutex> lk(mtx_);
        if (state_ != SessState::Running) {
            // 未鉴权业务报文一律丢弃（Doc14 §2.8 安全用例）
            SCLD_LOG(log_, LogLevel::Warn, "session",
                     "[session_id=" + session_id_ + "] 未鉴权业务报文，已丢弃 type="
                         + std::to_string(static_cast<int>(type)));
            return;
        }
    } break;
    default:
        return;
    }

    // Running 态分派（此时已释放锁）
    switch (type) {
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
        // 注入审计（被控知情权）：失败 Warn 即时落盘；成功 100:1 抽点防刷屏
        if (input_ != nullptr) {
            if (input_->handle(body, body_len)) {
                if ((++input_inj_count_ % 100) == 1) {
                    SCLD_LOG(log_, LogLevel::Info, "input",
                             "[session_id=" + session_id_
                                 + "] 键鼠注入中 inj_count="
                                 + std::to_string(input_inj_count_));
                }
            }
            else {
                SCLD_LOG(log_, LogLevel::Warn, "input",
                         "[session_id=" + session_id_ + "] 键鼠注入失败 kind="
                             + std::to_string(static_cast<int>(body[0])));
            }
        }
        break;
    case protocol::MsgType::ClipboardSync: {
        // 双端剪贴板同步（PRD P9）：解析后经事件上抛，平台层写本机剪贴板
        std::string text;
        if (protocol::parse_clipboard_sync(body, body_len, text) && bus_ != nullptr) {
            Evt_ClipboardReceived e;
            e.session_id = session_id_;
            e.text = std::move(text);
            e.timestamp_us = now_us();
            bus_->publish(e);
            audit_clipboard_once();  // 会话内首次同步记一条审计（去重）
        }
        break;
    }
    case protocol::MsgType::ScreenFrame: {
        // 控制端：帧头解析后经事件零拷贝上抛（解码渲染在 platform 层，Doc18 §4.2）
        protocol::ScreenFrameHeader hdr;
        const uint8_t* px = nullptr;
        size_t px_len = 0;
        if (!protocol::parse_screen_frame(body, body_len, hdr, px, px_len)) {
            break;  // 畸形帧丢弃
        }
        // 占位帧（Doc19 §4.2）data 恒空合法；普通帧空 data 仍按畸形丢弃
        const bool placeholder = (hdr.flags & 0x01) != 0;
        if (px == nullptr || (!placeholder && px_len == 0)) {
            break;
        }
        if (bus_ != nullptr) {
            Evt_VideoFrame e;
            e.payload = std::make_shared<std::vector<uint8_t>>(px, px + px_len);
            e.width = hdr.width;
            e.height = hdr.height;
            e.timestamp_us = hdr.timestamp_us;
            e.flags = hdr.flags;
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
        close_session(CloseReason::Error, SessionEndReason::NetworkError);
    }
    else if (st == TransportState::Idle) {
        // 对端断开（PRD P8 断连通知）：Running 收尾 + 等确认期复位弹窗，
        // 非 Idle 一律发 SessionClosed 供 UI 收连接条/提示
        const SessState cur = state();
        if (cur != SessState::Idle) {
            SCLD_LOG(log_, LogLevel::Warn, "session", "对端连接断开，会话复位");
            close_session(CloseReason::Normal, SessionEndReason::PeerQuit);
        }
    }
}

// ---------------- SESSION_CTRL 处理 ----------------

void RemoteSessionService::handle_key_sync(const uint8_t* body, size_t len) {
    std::vector<uint8_t> peer_pub, peer_nonce;
    if (!protocol::parse_key_sync(body, len, peer_pub, peer_nonce)) {
        SCLD_LOG(log_, LogLevel::Warn, "session", "KEY_SYNC 解析失败，丢弃");
        return;
    }
    Role role = Role::Controlled;
    std::vector<uint8_t> my_pub, my_nonce;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        // 已协商后拒绝重协商（防会话中途换钥攻击）；仅 Running 态处理
        if (encrypted_.load(std::memory_order_relaxed)
            || state_ != SessState::Running || crypto_ == nullptr) {
            return;
        }
        role = role_;
        my_pub = key_pub_;
        my_nonce = key_nonce_;
    }
    if (my_pub.size() != 64 || my_nonce.size() != 16) {
        return;  // 本端密钥对未就绪（enter_running 初始化失败已降级明文）
    }
    std::vector<uint8_t> secret;
    if (!crypto_->ecdh_shared(peer_pub, secret) || secret.size() != 32) {
        SCLD_LOG(log_, LogLevel::Warn, "session",
                 "[session_id=" + session_id_ + "] ECDH 共享秘密计算失败，本会话保持明文");
        return;
    }
    // nonce 拼接按 控制端→被控端 固定序，双端各自按角色取位（Doc 契约见 session_crypto.hpp）
    const std::vector<uint8_t>& nc =
        role == Role::Controller ? my_nonce : peer_nonce;
    const std::vector<uint8_t>& ns =
        role == Role::Controller ? peer_nonce : my_nonce;
    std::vector<uint8_t> key;
    if (!RemoteSessionCrypto::derive_session_key(*crypto_, secret, nc, ns, key)) {
        SCLD_LOG(log_, LogLevel::Warn, "session",
                 "[session_id=" + session_id_ + "] 会话密钥派生失败，本会话保持明文");
        return;
    }
    // 被控端：先明文应答 KEY_SYNC（对方尚未派生，必须明文），再激活本端加密
    if (role == Role::Controlled && transport_ != nullptr) {
        transport_->send_binary(
            protocol::build_key_sync(my_pub.data(), my_nonce.data()));
    }
    crypto_->set_key(key);
    bool expect = false;
    if (!encrypted_.compare_exchange_strong(expect, true)) {
        return;  // 并发竞态兜底（close_session 已复位）
    }
    SCLD_LOG(log_, LogLevel::Info, "session",
             "[session_id=" + session_id_ + "] 会话加密已启用 ECDH-P256/AES-256-GCM encrypted=yes");
}

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
        // 控制端收到同意：进入 Running。尾随 1B 附加（0=可控制 1=只读）——
        // 旧被控端单字节 Approved（无附加）按 0 处理（双向兼容）；
        // 只读=对端未授权键鼠：本地输入照常采集发送（省协议改动），由被控端丢弃
        const bool peer_ro = len > 1 && body[1] != 0;
        std::lock_guard<std::mutex> lk(mtx_);
        read_only_ = peer_ro;
        enter_running(false);
        break;
    }
    case protocol::CtrlSubType::Reject:
        // 控制端收到拒绝：按 reason 提示（Doc13 §3.3）；拒绝属审计日志（Warn 即时落盘）。
        // reason 原始数值随事件上抛，UI 经 friendly_reject 取映射文案（员工视图红线）
        SCLD_LOG(log_, LogLevel::Warn, "session",
                 "对端拒绝 reason=" + std::to_string(static_cast<int>(len > 1 ? body[1] : 0)));
        if (bus_ != nullptr) {
            Evt_ConnectStateChanged e;
            e.stage = "rejected";
            e.alias = peer_alias_;
            e.reject_reason = len > 1 ? static_cast<int>(body[1]) : 0;
            e.timestamp_us = now_us();
            bus_->publish(e);
        }
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

void RemoteSessionService::audit_event(const std::string& event,
                                       const std::string& result,
                                       const std::string& peer_code,
                                       const std::string& peer_ip,
                                       const std::string& note) {
    // audit.log 全量（七列，Doc19 §6）；主日志 Warn 摘要同步保留
    //（spdlog flush_on(Warn) 即时落盘，现场排障无需换文件）
    if (audit_ != nullptr) {
        audit_->audit(event, result, peer_code, peer_ip, note);
    }
    SCLD_LOG(log_, LogLevel::Warn, "audit",
             event + " result=" + result + " peer=" + (peer_code.empty() ? "-" : peer_code)
                 + " ip=" + (peer_ip.empty() ? "-" : peer_ip) + " " + note);
}

void RemoteSessionService::handle_request_connect(const protocol::RequestConnectBody& req) {
    // 对端连接码反查（黑名单/信任/审计共用一次）：走在线表，对端未上报
    // code（旧心跳）或不在表内则空串——三处判定统一按"未知即放行/人工"
    uint32_t peer_code = 0;
    if (aliases_ != nullptr) {
        for (const auto& dev : aliases_->online_devices()) {
            if (dev.alias == req.alias) {
                peer_code = dev.code;
                break;
            }
        }
    }
    const std::string code_str =
        peer_code != 0 ? std::to_string(peer_code) : "";
    const std::string req_ip =
        transport_ != nullptr ? transport_->peer_address() : "";

    // 黑名单过滤（Doc19 §3.4，P0）：命中即静默丢弃——不弹窗、不回
    // REJECT（不向拉黑方暴露存在性），仅审计。黑优先于单会话红线，
    // 占用中也不回 SessionBusy，彻底沉默
    if (blacklist_ != nullptr && !code_str.empty()
        && blacklist_->is_blocked(code_str)) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            peer_code_str_ = code_str;
            peer_ip_ = req_ip;
        }
        SCLD_LOG(log_, LogLevel::Warn, "session",
                 "[alias=" + req.alias + "][code=" + code_str
                     + "] 黑名单设备连接请求，已静默丢弃");
        audit_event("BlacklistHit", "Denied", code_str, req_ip,
                    "alias=" + req.alias);
        return;
    }

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
            audit_event("Request", "Busy", code_str, req_ip,
                        "alias=" + req.alias);
            return;
        }
        // 入站上下文缓存（审批/会话起止审计列数据源）
        peer_code_str_ = code_str;
        peer_ip_ = req_ip;
    }

    // 锁屏自动拒（Doc19 §1.3，P0）：无人监督场景防悄悄接入——信任直通
    // 同样拒绝（信任绕过人工确认，锁屏期间无人把关）；无人值守密码链路
    // 豁免（StartChallenge 即为无人值守设计，锁屏恰是它的使用场景）。
    // 探测失败由平台层 fail-open 返回未锁，不锁死远程可用性
    if (lock_checker_ != nullptr && lock_checker_()
        && (req.flags & protocol::connect_flags::kUnattended) == 0) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            reset_to_idle();  // 清上面缓存的审计列，回 Idle
        }
        if (transport_ != nullptr) {
            transport_->send_binary(
                protocol::build_reject(protocol::RejectReason::Other));
        }
        SCLD_LOG(log_, LogLevel::Warn, "session",
                 "[alias=" + req.alias + "] 本机锁屏，自动拒绝连接请求");
        audit_event("Request", "Denied", code_str, req_ip,
                    "锁屏自动拒 alias=" + req.alias);
        return;
    }

    // 版本协商（SessionCtrl 首包）：version>kProtoVersion 拒绝——本端不认识
    // 的新格式字段可能有语义歧义，宁拒不猜。version==0（老对端无该字节）
    // 视为 legacy 放行。请求未入状态机（state 仍 Idle），无看门狗残留
    if (req.version > protocol::kProtoVersion) {
        if (transport_ != nullptr) {
            transport_->send_binary(
                protocol::build_reject(protocol::RejectReason::Other));
        }
        SCLD_LOG(log_, LogLevel::Warn, "session",
                 "[alias=" + req.alias + "] 对端协议版本过高 v="
                     + std::to_string(req.version) + " > "
                     + std::to_string(protocol::kProtoVersion) + "，已拒绝");
        audit_event("Request", "Denied", code_str, req_ip,
                    "版本不兼容 v=" + std::to_string(req.version)
                        + " alias=" + req.alias);
        return;
    }

    session_id_ = make_session_id();
    peer_alias_ = req.alias;

    // 入站请求审计（Doc19 §6）：audit.log 全量 + 主日志 Warn 摘要
    audit_event("Request", "Pending", code_str, req_ip, "alias=" + req.alias);

    // 信任缓存直连（Doc18 §3.2）：对端连接码已记忆 → 免人工确认直接放行。
    // alias→code 反查走在线表；对端未上报 code（旧心跳）或不在表内则
    // 自然降级人工确认——信任判定永远以本机 trust.json 为准，不扩协议
    if (trust_ != nullptr && peer_code != 0) {
        if (trust_->is_trusted(std::to_string(peer_code))) {
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
            audit_event("AutoApprove", "OK", code_str, req_ip,
                        "trust直连 alias=" + req.alias);
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
    // PBKDF2 10 万迭代移交线程池（Doc12 §3）：传输层回调运行于 UI 主线程，
    // 同步派生会卡界面数百毫秒。入参按值捕获，完成后经线程安全 send 发出
    // （tx_ FIFO + QueuedConnection 排空）；服务析构先于池 join，共享指针
    // 按值捕获保证对象存活。密码清零后由 lambda 独占持有，发送即销毁
    const std::string salt(reinterpret_cast<const char*>(body), 16);
    const std::string challenge(reinterpret_cast<const char*>(body + 16), 16);
    auto transport = transport_;
    auto crypto = crypto_;
    auto logger = log_;
    const std::string sid = session_id_;
    pbkdf2_pool_.submit([this, transport, crypto, logger, password, salt,
                         challenge, sid] {
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<uint8_t> key;
        if (!crypto->derive_key(password, salt, key)) {
            SCLD_LOG(logger, LogLevel::Error, "session",
                     "[session_id=" + sid + "] 无人值守口令派生失败");
            return;
        }
        std::vector<uint8_t> proof;
        if (!crypto->hmac_sha256(
                std::string(reinterpret_cast<const char*>(key.data()), key.size()),
                challenge, proof)) {
            return;
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
        SCLD_LOG(logger, LogLevel::Info, "session",
                 "[session_id=" + sid + "] 无人值守 proof 已计算 PBKDF2="
                     + std::to_string(ms) + "ms 异步=1");
        if (transport != nullptr) {
            transport->send_binary(protocol::build_auth_proof(proof.data()));
        }
    });
}

void RemoteSessionService::handle_auth_proof(const uint8_t* body, size_t len) {
    if (unattended_auth_ == nullptr) {
        return;
    }
    std::string challenge;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        // 校验在途：PBKDF2 异步窗口内的重复 PROOF 直接吞掉（防并发重验）
        if (state_ != SessState::WaitForUnattended || auth_in_flight_) {
            return;
        }
        auth_in_flight_ = true;
        challenge = pending_challenge_;
    }
    const std::string proof(reinterpret_cast<const char*>(body), len);
    // verify_proof 含一次性挑战消费（authenticator 内部自持锁）+ PBKDF2 慢
    // 路径，整体移交线程池；结果回投主线程做状态迁移（bus 订阅方含 Qt 模型）
    auto auth = unattended_auth_;
    auto logger = log_;
    pbkdf2_pool_.submit([this, auth, logger, challenge, proof] {
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = auth->verify_proof(challenge, proof);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
        SCLD_LOG(logger, LogLevel::Info, "session",
                 "无人值守 proof 校验完成 耗时=" + std::to_string(ms)
                     + "ms ok=" + (ok ? "1" : "0") + " 异步=1");
        if (main_poster_) {
            main_poster_([this, ok, challenge] { finish_auth_proof(ok, challenge); });
        }
        else {
            finish_auth_proof(ok, challenge);  // 未注入：单测/headless 同步兜底
        }
    });
}

void RemoteSessionService::finish_auth_proof(bool ok, const std::string& challenge) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auth_in_flight_ = false;
        // 代际绑定：校验期间会话已断开重连（challenge 已换新）→ 本次结果作废
        if (state_ != SessState::WaitForUnattended || pending_challenge_ != challenge) {
            return;
        }
    }
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
    // 会话开始审计（Doc19 §6）：双角色（控制发起/被控接入）统一记
    audit_event("SessionStart", "OK", peer_code_str_, peer_ip_,
                unattended ? "无人值守接入" : "人工确认接入");
    // 会话密钥协商（PRD 会话加密）：生成 ECDH 临时密钥对 + 16B nonce。
    // 控制端立即发 KEY_SYNC（此刻仍明文）；被控端等对端 KEY_SYNC 到达后
    // 明文应答再激活加密（handle_key_sync）。失败降级明文并 Warn 审计
    if (crypto_ != nullptr) {
        std::vector<uint8_t> pub;
        if (crypto_->ecdh_keygen(pub) && pub.size() == 64) {
            static thread_local std::mt19937_64 rng{std::random_device{}()};
            std::vector<uint8_t> nonce(16);
            for (size_t i = 0; i < nonce.size(); i += 8) {
                const uint64_t r = rng();
                std::memcpy(nonce.data() + i, &r, std::min<size_t>(8, nonce.size() - i));
            }
            key_pub_ = pub;
            key_nonce_ = nonce;
            if (role_ == Role::Controller && transport_ != nullptr) {
                transport_->send_binary(
                    protocol::build_key_sync(pub.data(), nonce.data()));
            }
        }
        else {
            SCLD_LOG(log_, LogLevel::Warn, "session",
                     "[session_id=" + session_id_ + "] 密钥协商初始化失败，本会话降级明文传输");
        }
    }
    // 文件事件来源名（Evt_FileMetaReceived.device_alias 弹窗展示，Doc18 §5）
    if (files_ != nullptr) {
        files_->set_peer_alias(peer_alias_);
    }
    // 只读模式推送给注入器（P1）：Running 期被控端收 MOUSE_KEY 一律丢弃。
    // set_read_only 为 atomic store，持锁调用安全
    if (input_ != nullptr) {
        input_->set_read_only(read_only_);
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
        e.read_only = read_only_;  // 控制端徽标数据源（被控端恒 false）
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
        // 审计日志 Warn 级（spdlog flush_on(Warn) 即时落盘，Doc16 §2.5）
        SCLD_LOG(log_, LogLevel::Warn, "audit",
                 "[session_id=" + session_id_ + "][alias=" + peer_alias_
                     + "] 无人值守接入（密码校验通过，自动同意）");
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
    read_only_ = false;  // 只读态复位（下次会话重新按审批决定）
    clipboard_audit_done_.store(false);  // 剪贴板首次同步审计随会话复位
    // 会话加密复位（PRD）：清会话密钥与协商上下文，下次会话重新协商
    encrypted_.store(false, std::memory_order_relaxed);
    if (crypto_ != nullptr) {
        crypto_->set_key({});
    }
    key_pub_.clear();
    key_nonce_.clear();
    pending_challenge_.clear();
    auth_in_flight_ = false;  // 在途校验结果由代次绑定作废（finish 比对 challenge）
    peer_alias_.clear();
    pending_connect_ = {};
    session_id_.clear();
    // 对端审计缓存同步清（防下次会话前残留串号；approve→enter_running
    // 链条不经过本函数，不受影响）
    peer_code_str_.clear();
    peer_ip_.clear();
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

void RemoteSessionService::arm_outbound_watchdog(uint64_t my_seq) {
    // 前置：调用方不持锁。代次已由调用方在锁内递增，旧看门狗 pred 已成立，
    // 此处锁外 notify+join 让其自然退出，再拉起新线程
    confirm_cv_.notify_all();
    if (confirm_watchdog_.joinable()) {
        confirm_watchdog_.join();
    }
    confirm_watchdog_ = std::thread([this, my_seq] {
        std::unique_lock<std::mutex> lk(mtx_);
        // Approved/Rejected/取消/新发起任一发生（seq 变更或离开 WaitForUser）即退
        const bool consumed = confirm_cv_.wait_for(
            lk, std::chrono::seconds(kOutboundWaitSec), [this, my_seq] {
                return confirm_seq_ != my_seq || state_ != SessState::WaitForUser;
            });
        if (consumed || state_ != SessState::WaitForUser) {
            return;
        }
        reset_to_idle();  // 持锁调用安全（stop 均非阻塞）
        SCLD_LOG(log_, LogLevel::Warn, "session",
                 "等待对方确认 " + std::to_string(kOutboundWaitSec)
                     + "s 超时（对端忙或离线），连接已取消");
        if (bus_ != nullptr) {
            Evt_ConnectRequestTimeout e;
            e.timestamp_us = now_us();
            bus_->publish(e);  // UI 收连接状态条 + 提示
        }
    });
}

}  // namespace sc::localdesk
