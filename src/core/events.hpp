// ============================================================
// scToLocalDesk | src/core/events.hpp
// EventBus 领域事件定义（全部继承 IEvent 携带 trace 字段，Doc12 §2）
// ============================================================
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/interface/types.hpp"

namespace sc::localdesk {

/// 事件基类：结构化日志 trace 字段统一来源
struct IEvent {
    virtual ~IEvent() = default;

    std::string session_id;    ///< 会话 ID（无会话事件为空）
    std::string device_alias;  ///< 关联设备别名
    int64_t timestamp_us{0};   ///< 稳态时钟微秒
};

// ---------------- 发现（Doc07 §5） ----------------

struct Evt_DeviceOnline final : IEvent {
    std::string ip;
    uint16_t port{0};
};

struct Evt_DeviceOffline final : IEvent {
};

struct Evt_AliasConflict final : IEvent {
    std::string conflict_ip;   ///< 冲突方 IP
};

// ---------------- 传输（Doc08 §2） ----------------

struct Evt_TransportState final : IEvent {
    TransportState state{TransportState::Idle};
};

// ---------------- 会话（Doc09 §2） ----------------

enum class SessionEndReason {
    Normal,
    RejectedByUser,
    PasswordWrong,
    Timeout,
    Occupied,
    NetworkError,
    PeerQuit,
};

struct Evt_ConnectRequest final : IEvent {
    bool auto_approved{false};  ///< 无人值守鉴权通过，UI 只 Toast 不弹窗
    std::string peer_ip;        ///< 请求方 IP（确认弹窗展示，PRD 确认三件套）
};

/// 确认弹窗 30s 未响应自动拒（PRD 确认三件套）：UI 收到后关闭弹窗并提示
struct Evt_ConnectRequestTimeout final : IEvent {
};

/// 控制端发起被本地拦截（自连等）：不发网络包，UI 弹提示
struct Evt_ConnectDenied final : IEvent {
    std::string alias;
    std::string reason;  ///< 人类可读文案，直接展示
};

struct Evt_SessionStart final : IEvent {
    bool unattended{false};
    /// 只读模式（被控端审批未授权键鼠）：控制端据此显示只读徽标。
    /// 被控端恒 false（read_only 是对控制端授权的收紧）
    bool read_only{false};
};

/// 控制端连接过程状态（PRD P7 状态流转）：UI 据此展示细分文案/取消按钮
struct Evt_ConnectStateChanged final : IEvent {
    std::string stage;  ///< connecting=拨号中 / wait_confirm=等对方确认 / rejected=对方拒绝
    std::string alias;  ///< 目标别名
    /// REJECT reason 原始数值（stage=rejected 时有效，其余 -1）。
    /// UI 展示前须经 foundation::friendly_reject 映射（员工视图红线），
    /// 本事件只做原始值传递，不做文案拼装
    int reject_reason{-1};
};

struct Evt_SessionClosed final : IEvent {
    SessionEndReason reason{SessionEndReason::Normal};
    int64_t duration_us{0};
};

// ---------------- 推流（Doc10 §3） ----------------

struct Evt_StreamLag final : IEvent {
    uint64_t dropped_frames{0};
};

/// 控制端收到一帧画面（payload 为压缩数据或 RAW BGRA，shared_ptr 零拷贝流转；
/// 占位帧 payload 恒空，仅 flags 携带状态，Doc19 §4.2）
struct Evt_VideoFrame final : IEvent {
    std::shared_ptr<const std::vector<uint8_t>> payload;
    uint32_t width{0};
    uint32_t height{0};
    /// SCREEN_FRAME flags 透传：bit0=占位帧 bit1=锁屏占位
    uint8_t flags{0};
};

// ---------------- 剪贴板（PRD P9） ----------------

/// 远端剪贴板文本到达：平台层写入本机剪贴板（QSignalBlocker 防回环）
struct Evt_ClipboardReceived final : IEvent {
    std::string text;  ///< UTF-8 文本
};

// ---------------- 文件（Doc11 §2） ----------------

enum class FileFailReason {
    None = 0,
    Cancelled,
    DiskFull,
    IoError,
    Integrity,
    Unauthed,
};

/// 传输方向（UI ↑/↓ 标识；发送=本机推给对端）
enum class FileDir { Send = 0, Recv = 1 };

struct Evt_FileProgress final : IEvent {
    std::string file_id;
    std::string name;  ///< 文件名（UI 首次插条目用）
    uint64_t transferred{0};
    uint64_t total{0};
    FileDir dir{FileDir::Send};
};

/// 出站发送排队登记（Doc19 §5 P2）：send_file 超出并行上限入队即发；
/// UI 插「排队中」行，出队启动后由 FILE_META/progress 事件接管
struct Evt_FileQueued final : IEvent {
    std::string file_id;
    std::string name;
    uint64_t size{0};
    FileDir dir{FileDir::Send};
};

struct Evt_FileDone final : IEvent {
    std::string file_id;
    std::string name;
    FileDir dir{FileDir::Send};
};

struct Evt_FileFailed final : IEvent
{
    std::string file_id;
    std::string name;
    FileFailReason reason{FileFailReason::None};
    FileDir dir{FileDir::Send};
};

/// 对端请求发文件（confirm_before_transfer=true 时替代自动接收，Doc11 §4.3）
struct Evt_FileMetaReceived final : IEvent
{
    std::string file_id;
    std::string name;
    uint64_t size{0};
    std::string device_alias;  ///< 发送方连接名（会话层注入，弹窗展示）
};

// ---------------- 无人值守（Doc16 §2.5） ----------------

struct Evt_UnattendedLogin final : IEvent {
    int64_t started_at_us{0};
};

// ---------------- 配置（Doc17 §5） ----------------

struct Evt_ConfigChanged final : IEvent {
    std::string key;  ///< 变更项完整 key，如 "session.fps"
};

}  // namespace sc::localdesk
