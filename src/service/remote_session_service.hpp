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
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/interface/icrypto.hpp"
#include "core/interface/itransport.hpp"
#include "core/protocol/msg_type.hpp"

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

namespace protocol {
struct RequestConnectBody;
}

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
    [[nodiscard]] bool start_listening(uint16_t port);
    void stop_listening();

    // ---------------- 控制端 ----------------

    /// 发起协助：解析别名 → 连接 → 发 REQUEST_CONNECT
    /// unattended=true 时 password 为设置页输入的访问密码（瞬态，proof 发送后即清）
    [[nodiscard]] bool connect_to_peer(const std::string& alias, bool unattended,
                                       const std::string& password = "");

    // ---------------- UI 回调（人工确认弹窗，Doc13 §3.1） ----------------

    void approve();                                        ///< 弹窗点【同意】
    void reject(RejectReason reason = RejectReason::ManualDenied);

    // ---------------- 会话内 ----------------

    /// 一键断开（被控悬浮按钮 / 控制端关闭，均走此入口）
    void close_session(CloseReason reason = CloseReason::Normal);

    /// 会话中发送业务帧（画面/键鼠/文件），Running 前全部拒绝（Doc09 §3.1）
    [[nodiscard]] bool send_business_frame(const std::vector<uint8_t>& frame);

    void set_file_service(std::shared_ptr<FileService> files);
    void set_stream_service(std::shared_ptr<ScreenStreamService> stream);
    void set_input_service(std::shared_ptr<InputService> input);

    /// 应用设置页推流参数（Doc17 session.fps / session.jpeg_quality）
    void set_stream_params(int fps, int quality);

    [[nodiscard]] SessState state() const;
    [[nodiscard]] std::string session_id() const;

    // ---------------- Transport 回调（IO 线程触发） ----------------

    void on_transport_message(std::shared_ptr<const std::vector<uint8_t>> data);
    void on_transport_state(TransportState st);

private:
    void handle_session_ctrl(const uint8_t* body, size_t len);
    void handle_request_connect(const protocol::RequestConnectBody& req);
    void handle_auth_challenge(const uint8_t* body, size_t len);   ///< 控制端收挑战
    void handle_auth_proof(const uint8_t* body, size_t len);       ///< 被控端收证明
    void send_request_connect();                                   ///< 控制端连接后发申请
    void enter_running(bool unattended);
    void reset_to_idle();
    void publish_request_event(const std::string& peer_alias, bool auto_approved);

    std::shared_ptr<ITransport> transport_;
    std::shared_ptr<IAuthenticator> manual_auth_;
    std::shared_ptr<UnattendedPasswordAuthenticator> unattended_auth_;
    std::shared_ptr<ICrypto> crypto_;
    std::shared_ptr<AliasService> aliases_;
    std::shared_ptr<FileService> files_;
    std::shared_ptr<ScreenStreamService> stream_;
    std::shared_ptr<InputService> input_;
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
};

}  // namespace sc::localdesk
