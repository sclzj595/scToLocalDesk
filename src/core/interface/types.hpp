// ============================================================
// scToLocalDesk | src/core/interface/types.hpp
// 全局公共类型：DeviceInfo / VideoFrame / FileMeta / 鉴权（纯 C++，无 Qt）
// ============================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sc::localdesk {

/// 在线设备信息（UDP 心跳解析产物，见 Doc07）
struct DeviceInfo {
    std::string alias;        ///< 设备别名（UTF-8）
    std::string ip;           ///< 点分十进制 IPv4
    uint16_t port{0};         ///< WebSocket 监听端口
    uint16_t version{0};      ///< 协议版本（高 8 位主 / 低 8 位次）
    uint8_t status{0};        ///< 0x00 空闲可协助，0x01 会话中
    uint32_t code{0};         ///< 对端 9 位连接码（0=未知：旧包无可选 TLV，Doc18 §3.4）
    int64_t last_seen_us{0};  ///< 最近一次心跳时间（稳态时钟，微秒）
    bool manual{false};       ///< 手动添加对端（Doc19 §6.3）：跨子网 TCP 探活注入
};

/// 采集失败分类（Doc19 §4.2 占位帧）：grab 失败原因，占位提示文案分流依据
enum class CaptureErr : uint8_t {
    None = 0,          ///< 正常/无新帧
    DesktopLocked = 1, ///< 用户锁屏（输入桌面不可达）
    DriverError = 2,   ///< 采集驱动异常
};

/// 原始视频帧（采集线程产物，见 Doc10）
struct VideoFrame {
    const uint8_t* data{nullptr};
    size_t size{0};
    uint32_t width{0};
    uint32_t height{0};
    int64_t timestamp_us{0};
};

/// FILE_META flags 位定义（Doc19 §5 兼容扩展，末尾 1B，老对端恒 0）
constexpr uint8_t kFileFlagIsDir = 0x01;  ///< bit0=目录条目（size=0 无分片，接收侧 mkdir）

/// 文件传输元信息（见 Doc11 / Doc19 §5）
struct FileMeta {
    std::string file_id;      ///< 16B 随机 ID 的 hex 表示
    std::string name;         ///< 文件名（目录传输时为 '/' 分隔相对路径，根目录名作前缀）
    uint64_t size{0};
    uint32_t total_chunks{0};
    /// FILE_META flags（kFileFlagIsDir 等；老格式解析恒 0）
    uint8_t flags{0};
};

// ---------------- 鉴权相关（Doc06 §7 / Doc16） ----------------

struct AuthRequest {
    std::string peer_alias;
    bool unattended{false};   ///< 控制端请求走无人值守自动同意通道
};

enum class AuthResult {
    NeedManualConfirm,   ///< 抛给 UI 确认弹窗
    StartChallenge,      ///< 进入无人值守挑战应答
    Approved,
    Rejected,
};

/// 传输层连接状态（ITransport::OnStateCb 用，Doc08 §2）
enum class TransportState {
    Idle,
    Listening,
    Connecting,
    Connected,
    Error,
};

}  // namespace sc::localdesk
