// ============================================================
// scToLocalDesk | src/core/protocol/tlv.hpp
// TLV 报文编解码：header 读写 / 会话信令构建 / 半包粘包处理（Doc05 §1）
// 纯 C++ 无 Qt；Buffer 提供字节序列化
// ============================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/protocol/msg_type.hpp"

namespace sc::localdesk::protocol {

/// 解析报文头（不拷贝 body）
/// 输入 data 至少包含完整头部；body_len 为 Body 长度
/// 返回 nullopt 表示头部非法（长度不足）
std::optional<MsgType> parse_header(const uint8_t* data, size_t size,
                                                  uint32_t& body_len);

/// 判断是否已收到完整报文
/// @param data 已缓冲字节
/// @param[out] frame_total 完整报文总长（header+body），可整帧时有效
bool is_frame_complete(const uint8_t* data, size_t size, size_t& frame_total);

// ---------------- 报文构建（追加到 out） ----------------

/// 写报文头（body_len 已知场景）
void write_header(std::vector<uint8_t>& out, MsgType type, uint32_t body_len);

/// 构建整帧：type + body
std::vector<uint8_t> build_frame(MsgType type, const std::vector<uint8_t>& body);

// ---------------- 会话信令构建（Doc05 §3） ----------------

/// REQUEST_CONNECT：alias(u8len) + flags(1B) + [proof_len(2B)+proof]
std::vector<uint8_t> build_request_connect(std::string_view alias,
                                                         uint8_t flags);

/// AUTH_CHALLENGE：salt(16B) + challenge(16B)（salt 供控制端 PBKDF2 派生 K，密码不上网）
std::vector<uint8_t> build_auth_challenge(const uint8_t* salt16,
                                                        const uint8_t* challenge16);

/// AUTH_PROOF：proof(32B)
std::vector<uint8_t> build_auth_proof(const uint8_t* proof32);

/// FILE_META：file_id(16B) + name_len(u16BE) + name + size(u64BE) + total_chunks(u32BE)
std::vector<uint8_t> build_file_meta(std::string_view file_id_hex,
                                                   std::string_view name,
                                                   uint64_t size, uint32_t total_chunks);

struct FileMetaBody {
    std::string file_id;
    std::string name;
    uint64_t size{0};
    uint32_t total_chunks{0};
};

bool parse_file_meta(const uint8_t* body, size_t len, FileMetaBody& out);

/// 单字节控制报文：APPROVED / KEEPALIVE
std::vector<uint8_t> build_ctrl(CtrlSubType sub);

/// REJECT：reason(1B)
std::vector<uint8_t> build_reject(RejectReason reason);

/// CLOSE：reason(1B)
std::vector<uint8_t> build_close(CloseReason reason);

// ---------------- REQUEST_CONNECT body 解析 ----------------

struct RequestConnectBody {
    std::string alias;
    uint8_t flags{0};
};

/// 从 body 解析 REQUEST_CONNECT（不校验头，调用方先 parse_header）
bool parse_request_connect(const uint8_t* body, size_t len,
                                         RequestConnectBody& out);

// ---------------- 键鼠帧（MOUSE_KEY，Doc05 §1） ----------------

enum class MouseKeyKind : uint8_t {
    Mouse = 0,
    Keyboard = 1,
};

struct MouseKeyBody {
    MouseKeyKind kind{MouseKeyKind::Mouse};
    // 鼠标：action 见 IInputInjector::MouseAction，x/y 为 0~65535 归一化
    uint8_t action{0};
    uint16_t x{0};
    uint16_t y{0};
    int16_t wheel{0};
    // 键盘
    uint16_t vk{0};
    uint16_t scan{0};
    uint8_t down{0};
};

std::vector<uint8_t> build_mouse_key(const MouseKeyBody& mk);

bool parse_mouse_key(const uint8_t* body, size_t len, MouseKeyBody& out);

// ---------------- 画面帧（SCREEN_FRAME，Doc05 §1） ----------------

struct ScreenFrameHeader {
    uint32_t width{0};
    uint32_t height{0};
    int64_t timestamp_us{0};
};

/// body = width(4B BE) + height(4B BE) + timestamp(8B BE) + data
std::vector<uint8_t> build_screen_frame(const ScreenFrameHeader& hdr,
                                                      const uint8_t* data, size_t len);

/// 解析帧头并返回数据指针（零拷贝视图，不越过 body 生命周期）
bool parse_screen_frame(const uint8_t* body, size_t len,
                                      ScreenFrameHeader& hdr, const uint8_t*& data,
                                      size_t& data_len);

}  // namespace sc::localdesk::protocol
