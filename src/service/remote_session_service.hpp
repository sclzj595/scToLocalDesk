// ============================================================
// scToLocalDesk | src/service/remote_session_service.hpp
// 远程会话服务：单会话状态机 + 鉴权路由（Doc09）
//   Idle ─▶ PendingApprove{WaitForUser | WaitForUnattended} ─▶ Running ─▶ Closing ─▶ Idle
// 控制端链路：connect_to_peer → Connected → REQUEST_CONNECT → (挑战应答|人工弹窗) → Running
// 被控端链路：start_listening → REQUEST_CONNECT 到达 → 路由鉴权 → APPROVED/REJECT
// 鉴权红线：APPROVED 前不推画面 / 不收键鼠 / 不收文件
// ============================================================
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/interface/icrypto.hpp"
#include "core/interface/itransport.hpp"
#include "core/protocol/msg_type.hpp"
#include "core/events.hpp"  // SessionEndReason（close_session 默认参数需完整类型，PRD P8）
#include "foundation/thread_pool.hpp"

namespace sc::localdesk {

class IAuthenticator;
class UnattendedPasswordAuthenticator;
class AliasService;
class FileService;
class ScreenStreamService;
class InputService;

namespace foundation {
class EventBus;
class ILogger;
}
class TrustStore;

namespace protocol {
struct RequestConnectBody;
struct MouseKeyBody;
}


/// 人工确认弹窗超时（PRD 确认三件套）：超时自动拒绝，对端收 REJECT(Timeout)
constexpr int kConfirmTimeoutSec = 30;
constexpr int kOutboundWaitSec = 35;  // 控制端等确认上限：>对端 30s 看门狗，让其 REJECT 先到

// 协议枚举直接以短名暴露给服务层
using protocol::CloseReason;
using protocol::RejectReason;

class RemoteSessionService final {
public:
    /// 会话状态（Doc09 §2）
    enum class SessState : int
    {
        Idle = 0,
        WaitForUser,        ///< 人工模式：等 ConfirmDialog
        WaitForUnattended,  ///< 无人值守：挑战已发，等 PROOF
        Running,
        Closing,
    };

    /// 本端角色（同一 EXE 二合一：被控监听即 Controlled，主动发起即 Controller）
    enum class Role : int
    {
        Controlled = 0,
        Controller = 1,
    };

    RemoteSessionService(std::shared_ptr<ITransport> transport,
                         std::shared_ptr<IAuthenticator> manual_auth,
                         std::shared_ptr<UnattendedPasswordAuthenticator> unattended_auth,
                         std::shared_ptr<ICrypto> crypto,
                         std::shared_ptr<AliasService> aliases,
                         std::shared_ptr<foundation::EventBus> bus,
                         std::shared_ptr<foundation::ILogger> logger);

    ~RemoteSessionService();

    // ---------------- 被控端 ----------------

    /// 启动 WebSocket 监听并接管 Transport 回调（Doc09 §2）
    bool start_listening(uint16_t port);
    void stop_listening();

    // ---------------- 控制端 ----------------

    /// 发起协助：解析别名 → 连接 → 发 REQUEST_CONNECT
    /// unattended=true 时 password 为设置页输入的访问密码（瞬态，proof 发送后即清）
    bool connect_to_peer(const std::string& alias, bool unattended,                                       const std::string& password = "");

    // ---------------- UI 回调（人工确认弹窗，Doc13 §3.1） ----------------

    void approve();                                        ///< 弹窗点【同意】
    void reject(RejectReason reason = RejectReason::ManualDenied);

    // ---------------- 会话内 ----------------

    /// 一键断开（被控悬浮按钮 / 控制端关闭，均走此入口）。
    /// end_reason：UI 事件语义（对端断开=PeerQuit / 网络错=NetworkError），
    /// close_session 是 Evt_SessionClosed 唯一发布点（PRD P8 断连通知收敛）
    void close_session(CloseReason reason = CloseReason::Normal,
                       SessionEndReason end_reason = SessionEndReason::Normal);

    /// 会话中发送业务帧（画面/键鼠/文件），Running 前全部拒绝（Doc09 §3.1）
    bool send_business_frame(const std::vector<uint8_t>& frame);

    /// 控制端键鼠事件出口（Doc18 §4.4）：MouseKeyBody → MOUSE_KEY 帧，Running 红线内
    bool send_input_event(const protocol::MouseKeyBody& mk);

    /// 剪贴板文本同步（PRD P9）：本机剪贴板变化 → CLIPBOARD_SYNC 帧，
    /// Running 红线内；空文本/超限（kClipboardMaxBytes）丢弃
    bool send_clipboard(const std::string& text);

    void set_file_service(std::shared_ptr<FileService> files);
    void set_stream_service(std::shared_ptr<ScreenStreamService> stream);
    void set_input_service(std::shared_ptr<InputService> input);
    /// 信任缓存注入（Doc18 §3.2）：被控端对已记忆设备免人工确认直连；
    /// 可空（未注入则全部请求走人工确认）
    void set_trust_store(TrustStore* ts) {
        trust_ = ts;
    }

    /// 自连拦截（PRD 连接黑名单）：注入本机判定回调（IP 属本机集合 且 端口==本机监听端口）；
    /// connect_to_peer 拨号前比对，命中即本地拒绝不发网络包。
    /// 双实例同机（端口不同）不算自连，放行保 E2E 回环
    void set_self_ip_checker(
        std::function<bool(const std::string& ip, uint16_t port)> checker) {
        self_ip_checker_ = std::move(checker);
    }

    /// 主线程回投（PBKDF2 异步收尾用）：服务层 Qt-free，投递能力由装配层注入
    /// （main.cpp 经 QMetaObject::invokeMethod(qApp)）。未注入时异步完成同步兜底
    /// （单测/headless）。校验后的状态迁移/事件发布必须在主线程——bus 订阅方
    /// 含 Qt 模型，非全线程安全
    void set_main_poster(std::function<void(std::function<void()>)> poster) {
        main_poster_ = std::move(poster);
    }

    /// 应用设置页推流参数（Doc17 session.fps / session.jpeg_quality）
    void set_stream_params(int fps, int quality);

    SessState state() const;
    std::string session_id() const;

    // ---------------- Transport 回调（IO 线程触发） ----------------

    void on_transport_message(std::shared_ptr<const std::vector<uint8_t>> data);
    void on_transport_state(TransportState st);

private:
    void handle_session_ctrl(const uint8_t* body, size_t len);
    void handle_key_sync(const uint8_t* body, size_t len);  ///< 密钥协商应答（PRD 会话加密）
    void finish_auth_proof(bool ok, const std::string& challenge);  ///< 校验完成回投（仅主线程）
    void handle_request_connect(const protocol::RequestConnectBody& req);
    void handle_auth_challenge(const uint8_t* body, size_t len);   ///< 控制端收挑战
    void handle_auth_proof(const uint8_t* body, size_t len);       ///< 被控端收证明
    void send_request_connect();                                   ///< 控制端连接后发申请
    void enter_running(bool unattended);
    void reset_to_idle();
    void publish_request_event(const std::string& peer_alias, bool auto_approved);
    /// 武装确认看门狗（PRD 确认三件套）：kConfirmTimeoutSec 内弹窗未消费
    /// 则自动拒绝。前置：调用方不持锁（内部 join 旧线程需让路）
    void arm_confirm_watchdog(uint64_t my_seq);
    /// 武装控制端出站看门狗：发起协助后 kOutboundWaitSec 内未收到
    /// Approved/Rejected（对端忙/离线/REJECT 丢失）则复位 Idle，
    /// 根治「没连接但一直提示有会话」卡死（2026-10-28 双机实战踩坑）
    void arm_outbound_watchdog(uint64_t my_seq);

public:
    /// 控制端取消连接（PRD P7）：拨号/等确认期可取消；Running 不响应
    void cancel_connect();
    /// 最近一次成功发起的目标别名（重连按钮数据源）；无则空
    std::string last_peer() const {
        return last_peer_;
    }
    /// 是否可发起新会话（PRD P7 防重复）：单会话红线，Idle 才可发起
    bool can_start_control() const;

    std::shared_ptr<ITransport> transport_;
    std::shared_ptr<IAuthenticator> manual_auth_;
    std::shared_ptr<UnattendedPasswordAuthenticator> unattended_auth_;
    std::shared_ptr<ICrypto> crypto_;
    std::shared_ptr<AliasService> aliases_;
    std::shared_ptr<FileService> files_;
    std::shared_ptr<ScreenStreamService> stream_;
    std::shared_ptr<InputService> input_;
    uint64_t input_inj_count_{0};  ///< 注入审计抽点计数（RemoteSession 线程，单写者）
    TrustStore* trust_{nullptr};
    std::shared_ptr<foundation::EventBus> bus_;
    std::shared_ptr<foundation::ILogger> log_;

    // 控制端待发申请上下文（connect 成功后由 Connected 回调消费）
    struct PendingConnect
    {
        std::string alias;
        bool unattended{false};
        std::string password;  ///< 明文瞬态：proof 发送后立即清除（Doc16 §2.4）
        bool valid{false};
    };

    mutable std::mutex mtx_;
    SessState state_{SessState::Idle};
    Role role_{Role::Controlled};
    int stream_fps_{15};
    int stream_quality_{80};
    std::string session_id_;
    std::string peer_alias_;
    std::string pending_challenge_;   ///< 被控端已签发挑战 hex
    PendingConnect pending_connect_;  ///< 控制端待发申请
    bool listening_{false};
    std::atomic<int64_t> session_seq_{0};
    std::function<bool(const std::string&, uint16_t)> self_ip_checker_;  ///< 自连拦截判定（空=不拦）
    std::string last_peer_;  ///< 最近发起目标（重连用；连接期赋值）

    // 会话密钥协商（PRD 会话加密）：Running 时生成临时密钥对 + nonce，
    // KEY_SYNC 交换派生 AES-256-GCM 会话密钥；encrypted_ 为收发激活开关
    std::atomic<bool> encrypted_{false};
    std::vector<uint8_t> key_pub_;   ///< 本端 ECDH 裸公钥（64B，持锁读写）
    std::vector<uint8_t> key_nonce_; ///< 本端 16B nonce（持锁读写）

    // 确认看门狗（PRD 确认三件套）：WaitForUser 期间 30s 无响应自动拒。
    // 代次模型：每次进入 WaitForUser 分配新 seq；approve/reject/析构使旧代次
    // 失效（seq 变化），看门狗醒来即退——同机双实例/复用连接均无串话窗口
    std::condition_variable confirm_cv_;
    uint64_t confirm_seq_{0};       ///< 当前弹窗代次（持锁变更）
    std::thread confirm_watchdog_;  ///< 仅在 arm/析构处 join（不持锁）

    // PBKDF2 异步（Doc12 §3 线程池红线）：口令派生/校验移交池线程，
    // 传输层回调（UI 主线程）不再阻塞数百毫秒。完成经 main_poster_ 回投
    // 主线程做状态迁移（未注入则同步兜底）。声明在成员区末尾——先于其他
    // 成员析构，池 join 时任务引用的共享指针成员仍存活
    std::function<void(std::function<void()>)> main_poster_;  ///< 异步完成回投主线程
    foundation::ThreadPool pbkdf2_pool_{2};  ///< 口令派生/校验专用（CPU 密集 2 线程足矣）
    bool auth_in_flight_{false};  ///< 无人值守校验在途（持锁，吞窗口内重复 PROOF）
};

}  // namespace sc::localdesk
