// ============================================================
// scToLocalDesk | src/core/protocol/msg_type.hpp
// TLV 协议常量：MsgType / SESSION_CTRL 子类型 / REQUEST_CONNECT flags
// （契约唯一基准：docs/Doc05_Protocol.md）
// ============================================================
#pragma once

#include <cstdint>

namespace sc::localdesk::protocol {

/// 报文头：[ 1B MsgType ][ 4B BE BodyLen ][ Body ]
enum class MsgType : uint8_t {
    MouseKey     = 0x01,  ///< 键鼠事件
    ScreenFrame  = 0x02,  ///< 屏幕编码帧
    FileChunk    = 0x03,  ///< 文件分片
    SessionCtrl  = 0x04,  ///< 会话控制
    ConfigSync   = 0x05,  ///< 配置同步（预留）
    FileMeta     = 0x06,  ///< 文件元信息（file_id/name/size/total_chunks，Doc05 §5）
    ClipboardSync = 0x07, ///< 剪贴板文本同步（PRD P9：text_len(u32BE)+text）
    KeySync      = 0x08,  ///< 会话密钥协商（PRD：pub 64B ECDH 裸坐标 + nonce 16B）
};

/// SESSION_CTRL Body 首字节 ctrl_subtype（Doc05 §3）
enum class CtrlSubType : uint8_t {
    RequestConnect = 0x01,
    Approved       = 0x02,
    Reject         = 0x03,
    Close          = 0x04,
    Keepalive      = 0x05,
    AuthChallenge  = 0x06,  ///< salt(16B)+challenge(16B)（无人值守；salt 供控制端派生 K）
    AuthProof      = 0x07,  ///< 32B HMAC
};

/// REJECT reason（Doc05 §3）
enum class RejectReason : uint8_t {
    ManualDenied  = 0x00,  ///< 人工拒绝
    PasswordWrong = 0x01,  ///< 无人值守密码校验失败
    SessionBusy   = 0x02,  ///< 会话占用（单会话红线）
    Other         = 0x03,  ///< 其他（含无人值守开关关闭）
    Timeout       = 0x04,  ///< 确认弹窗 30s 未响应，自动拒绝（PRD 确认三件套）
};

/// CLOSE reason
enum class CloseReason : uint8_t {
    Normal = 0x00,
    Error  = 0x01,
};

/// REQUEST_CONNECT flags 位定义（Doc05 §3）
namespace connect_flags {
constexpr uint8_t kUnattended = 0x01;  ///< bit0：请求无人值守通道
constexpr uint8_t kReservedAll = 0xFE;
}  // namespace connect_flags

/// 文件分片字段偏移（FILE_CHUNK Body，Doc05 §5）：16B file_id + 4B idx + 4B total + 1B last + 4B len
constexpr size_t kFileChunkHeaderSize = 16 + 4 + 4 + 1 + 4;

/// 报文头尺寸
constexpr size_t kHeaderSize = 1 + 4;

}  // namespace sc::localdesk::protocol
