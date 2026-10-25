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
};

/// 控制端发起被本地拦截（自连等）：不发网络包，UI 弹提示
struct Evt_ConnectDenied final : IEvent {
    std::string alias;
    std::string reason;  ///< 人类可读文案，直接展示
};

struct Evt_SessionStart final : IEvent {
    bool unattended{false};
};

struct Evt_SessionClosed final : IEvent {
    SessionEndReason reason{SessionEndReason::Normal};
    int64_t duration_us{0};
};

// ---------------- 推流（Doc10 §3） ----------------

struct Evt_StreamLag final : IEvent {
    uint64_t dropped_frames{0};
};

/// 控制端收到一帧画面（payload 为压缩数据或 RAW BGRA，shared_ptr 零拷贝流转）
struct Evt_VideoFrame final : IEvent {
    std::shared_ptr<const std::vector<uint8_t>> payload;
    uint32_t width{0};
    uint32_t height{0};
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
