// ============================================================
// scToLocalDesk | src/core/protocol/tlv.cpp
// TLV 报文编解码实现（大端序，Doc05 §1）
// ============================================================
#include "core/protocol/tlv.hpp"

namespace sc::localdesk::protocol {

namespace {

void push_be16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

void push_be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

void push_str8(std::vector<uint8_t>& v, const std::string& s) {
    v.push_back(static_cast<uint8_t>(s.size()));
    v.insert(v.end(), s.begin(), s.end());
}

}  // namespace

bool parse_header(const uint8_t* data, size_t size, uint32_t& body_len,
                  MsgType& type) {
    if (data == nullptr || size < kHeaderSize) {
        return false;
    }
    body_len = (static_cast<uint32_t>(data[1]) << 24)
             | (static_cast<uint32_t>(data[2]) << 16)
             | (static_cast<uint32_t>(data[3]) << 8)
             | static_cast<uint32_t>(data[4]);
    type = static_cast<MsgType>(data[0]);
    return true;
}

bool is_frame_complete(const uint8_t* data, size_t size, size_t& frame_total) {
    uint32_t body_len = 0;
    MsgType t;
    if (!parse_header(data, size, body_len, t)) {
        return false;
    }
    frame_total = kHeaderSize + body_len;
    return size >= frame_total;
}

void write_header(std::vector<uint8_t>& out, MsgType type, uint32_t body_len) {
    out.push_back(static_cast<uint8_t>(type));
    push_be32(out, body_len);
}

std::vector<uint8_t> build_frame(MsgType type, const std::vector<uint8_t>& body) {
    std::vector<uint8_t> out;
    out.reserve(kHeaderSize + body.size());
    write_header(out, type, static_cast<uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

std::vector<uint8_t> build_request_connect(const std::string& alias, uint8_t flags,
                                           uint8_t version) {
    std::vector<uint8_t> body;
    body.reserve(1 + 1 + alias.size() + 1 + 1);
    // SESSION_CTRL body 首字节恒为 sub（Doc05 §3；此前漏写致被控端当未知 sub 丢弃）
    body.push_back(static_cast<uint8_t>(CtrlSubType::RequestConnect));
    push_str8(body, alias);
    body.push_back(flags);
    // 末尾追加 1B 协议版本：老对端按 alias_len+flags 解析，多余字节忽略——
    // 天然兼容；新对端读出做版本协商（version>kProtoVersion 拒绝）
    body.push_back(version);
    return build_frame(MsgType::SessionCtrl, body);
}

std::vector<uint8_t> build_auth_challenge(const uint8_t* salt16, const uint8_t* challenge16) {
    std::vector<uint8_t> body;
    body.reserve(1 + 16 + 16);
    body.push_back(static_cast<uint8_t>(CtrlSubType::AuthChallenge));
    body.insert(body.end(), salt16, salt16 + 16);
    body.insert(body.end(), challenge16, challenge16 + 16);
    return build_frame(MsgType::SessionCtrl, body);
}

std::vector<uint8_t> build_auth_proof(const uint8_t* proof32) {
    std::vector<uint8_t> body;
    body.reserve(1 + 32);
    body.push_back(static_cast<uint8_t>(CtrlSubType::AuthProof));
    body.insert(body.end(), proof32, proof32 + 32);
    return build_frame(MsgType::SessionCtrl, body);
}

std::vector<uint8_t> build_file_meta(const std::string& file_id_hex, const std::string& name,
                                     uint64_t size, uint32_t total_chunks, uint8_t flags) {
    std::vector<uint8_t> body;
    body.reserve(16 + 2 + name.size() + 8 + 4 + 1);
    // file_id：hex 字符串转 16B
    for (size_t i = 0; i + 1 < file_id_hex.size() && body.size() < 16; i += 2) {
        const auto nib = [](char c) -> uint8_t {
            return c <= '9' ? static_cast<uint8_t>(c - '0')
                            : static_cast<uint8_t>((c | 0x20) - 'a' + 10);
        };
        body.push_back(static_cast<uint8_t>((nib(file_id_hex[i]) << 4)
                                            | nib(file_id_hex[i + 1])));
    }
    push_be16(body, static_cast<uint16_t>(name.size()));
    body.insert(body.end(), name.begin(), name.end());
    for (int s = 56; s >= 0; s -= 8) {
        body.push_back(static_cast<uint8_t>(size >> s));
    }
    push_be32(body, total_chunks);
    // flags（Doc19 §5 目录传输）：=0 走老格式（字节不变），≠0 末尾追加 1B——
    // 老对端按固定布局解析多余字节忽略，天然兼容（ScreenFrameHeader 同款）
    if (flags != 0) {
        body.push_back(flags);
    }
    return build_frame(MsgType::FileMeta, body);
}

bool parse_file_meta(const uint8_t* body, size_t len, FileMetaBody& out) {
    if (body == nullptr || len < 16 + 2 + 8 + 4) {
        return false;
    }
    static constexpr char kHex[] = "0123456789abcdef";
    out.file_id.reserve(32);
    for (size_t i = 0; i < 16; ++i) {
        out.file_id.push_back(kHex[body[i] >> 4]);
        out.file_id.push_back(kHex[body[i] & 0x0F]);
    }
    size_t pos = 16;
    const uint16_t name_len = static_cast<uint16_t>((body[pos] << 8) | body[pos + 1]);
    pos += 2;
    if (pos + name_len + 12 > len) {
        return false;
    }
    out.name.assign(reinterpret_cast<const char*>(body) + pos, name_len);
    pos += name_len;
    out.size = 0;
    for (size_t i = 0; i < 8; ++i) {
        out.size = (out.size << 8) | body[pos + i];
    }
    pos += 8;
    out.total_chunks = (static_cast<uint32_t>(body[pos]) << 24)
                     | (static_cast<uint32_t>(body[pos + 1]) << 16)
                     | (static_cast<uint32_t>(body[pos + 2]) << 8)
                     | static_cast<uint32_t>(body[pos + 3]);
    pos += 4;
    // 末尾 flags（Doc19 §5 目录传输）：新格式 = 基础布局 + 1B，老格式（长度恰好
    // 等于基础布局）恒 0——按 len 判别，不因多余/缺失字节失败（向后兼容老对端）
    out.flags = 0;
    if (len > pos) {
        out.flags = body[pos];
    }
    return true;
}

std::vector<uint8_t> build_clipboard_sync(const std::string& text) {
    if (text.size() > kClipboardMaxBytes) {
        return {};  // 超限拒发（防大位图挤占传输队列）
    }
    std::vector<uint8_t> body;
    body.reserve(4 + text.size());
    push_be32(body, static_cast<uint32_t>(text.size()));
    body.insert(body.end(), text.begin(), text.end());
    return build_frame(MsgType::ClipboardSync, body);
}

std::vector<uint8_t> build_key_sync(const uint8_t* pub64, const uint8_t* nonce16) {
    if (pub64 == nullptr || nonce16 == nullptr) {
        return {};
    }
    std::vector<uint8_t> body;
    body.reserve(80);
    body.insert(body.end(), pub64, pub64 + 64);
    body.insert(body.end(), nonce16, nonce16 + 16);
    return build_frame(MsgType::KeySync, body);
}

bool parse_key_sync(const uint8_t* body, size_t len,
                    std::vector<uint8_t>& pub,
                    std::vector<uint8_t>& nonce) {
    if (body == nullptr || len != 80) {
        return false;
    }
    pub.assign(body, body + 64);
    nonce.assign(body + 64, body + 80);
    return true;
}

bool parse_clipboard_sync(const uint8_t* body, size_t len, std::string& out) {
    out.clear();
    if (body == nullptr || len < 4) {
        return false;
    }
    const uint32_t n = (static_cast<uint32_t>(body[0]) << 24)
                     | (static_cast<uint32_t>(body[1]) << 16)
                     | (static_cast<uint32_t>(body[2]) << 8)
                     | static_cast<uint32_t>(body[3]);
    if (n > kClipboardMaxBytes || 4u + n > len) {
        return false;  // 超限或截断
    }
    out.assign(reinterpret_cast<const char*>(body) + 4, n);
    return true;
}

std::vector<uint8_t> build_ctrl(CtrlSubType sub) {
    const std::vector<uint8_t> body{static_cast<uint8_t>(sub)};
    return build_frame(MsgType::SessionCtrl, body);
}

std::vector<uint8_t> build_ctrl(CtrlSubType sub, uint8_t extra) {
    const std::vector<uint8_t> body{static_cast<uint8_t>(sub), extra};
    return build_frame(MsgType::SessionCtrl, body);
}

std::vector<uint8_t> build_reject(RejectReason reason) {
    const std::vector<uint8_t> body{static_cast<uint8_t>(CtrlSubType::Reject),
                                    static_cast<uint8_t>(reason)};
    return build_frame(MsgType::SessionCtrl, body);
}

std::vector<uint8_t> build_close(CloseReason reason) {
    const std::vector<uint8_t> body{static_cast<uint8_t>(CtrlSubType::Close),
                                    static_cast<uint8_t>(reason)};
    return build_frame(MsgType::SessionCtrl, body);
}

bool parse_request_connect(const uint8_t* body, size_t len, RequestConnectBody& out) {
    if (body == nullptr || len < 4) {  // 至少 sub(1) + alias_len(1) + flags(1) + 1 字节别名
        return false;
    }
    if (body[0] != static_cast<uint8_t>(CtrlSubType::RequestConnect)) {
        return false;
    }
    const uint8_t alias_len = body[1];
    if (alias_len == 0 || static_cast<size_t>(2) + alias_len + 1 > len) {
        return false;
    }
    out.alias.assign(reinterpret_cast<const char*>(body) + 2, alias_len);
    out.flags = body[2 + alias_len];
    // 协议版本（新格式末尾追加 1B）：老格式无该字节按 0（legacy），
    // 解析不因多余/缺失字节失败——向后兼容老对端
    out.version = 0;
    if (static_cast<size_t>(3) + alias_len < len) {
        out.version = body[3 + alias_len];
    }
    return true;
}

std::vector<uint8_t> build_mouse_key(const MouseKeyBody& mk) {
    std::vector<uint8_t> body;
    body.reserve(8);
    body.push_back(static_cast<uint8_t>(mk.kind));
    if (mk.kind == MouseKeyKind::Mouse) {
        // kind + action + x + y + wheel
        body.push_back(mk.action);
        push_be16(body, mk.x);
        push_be16(body, mk.y);
        body.push_back(static_cast<uint8_t>(static_cast<uint16_t>(mk.wheel) >> 8));
        body.push_back(static_cast<uint8_t>(static_cast<uint16_t>(mk.wheel)));
    } else {
        // kind + vk + scan + down
        push_be16(body, mk.vk);
        push_be16(body, mk.scan);
        body.push_back(mk.down);
    }
    return build_frame(MsgType::MouseKey, body);
}

bool parse_mouse_key(const uint8_t* body, size_t len, MouseKeyBody& out) {
    if (body == nullptr || len < 1) {
        return false;
    }
    const auto kind = static_cast<MouseKeyKind>(body[0]);
    if (kind == MouseKeyKind::Mouse) {
        if (len < 1 + 1 + 2 + 2 + 2) {
            return false;
        }
        out.kind = kind;
        out.action = body[1];
        out.x = static_cast<uint16_t>((body[2] << 8) | body[3]);
        out.y = static_cast<uint16_t>((body[4] << 8) | body[5]);
        out.wheel = static_cast<int16_t>((body[6] << 8) | body[7]);
        return true;
    }
    if (len < 1 + 2 + 2 + 1) {
        return false;
    }
    out.kind = kind;
    out.vk = static_cast<uint16_t>((body[1] << 8) | body[2]);
    out.scan = static_cast<uint16_t>((body[3] << 8) | body[4]);
    out.down = body[5];
    return true;
}

std::vector<uint8_t> build_screen_frame(const ScreenFrameHeader& hdr, const uint8_t* data,
                                        size_t len) {
    std::vector<uint8_t> body;
    // flags≠0（占位帧，data 恒空）末尾追加 1B flags → body 恒 17B（新格式）；
    // flags=0 走老格式 16+data，老对端逐字节兼容（头 16B 布局不动）
    body.reserve(16 + len + 1);
    push_be32(body, hdr.width);
    push_be32(body, hdr.height);
    for (int s = 56; s >= 0; s -= 8) {
        body.push_back(static_cast<uint8_t>(static_cast<uint64_t>(hdr.timestamp_us) >> s));
    }
    if (data != nullptr && len > 0) {
        body.insert(body.end(), data, data + len);
    }
    if (hdr.flags != 0) {
        body.push_back(hdr.flags);
    }
    return build_frame(MsgType::ScreenFrame, body);
}

bool parse_screen_frame(const uint8_t* body, size_t len, ScreenFrameHeader& hdr,
                        const uint8_t*& data, size_t& data_len) {
    if (body == nullptr || len < 16) {
        return false;
    }
    hdr.width = (static_cast<uint32_t>(body[0]) << 24)
              | (static_cast<uint32_t>(body[1]) << 16)
              | (static_cast<uint32_t>(body[2]) << 8)
              | static_cast<uint32_t>(body[3]);
    hdr.height = (static_cast<uint32_t>(body[4]) << 24)
               | (static_cast<uint32_t>(body[5]) << 16)
               | (static_cast<uint32_t>(body[6]) << 8)
               | static_cast<uint32_t>(body[7]);
    hdr.timestamp_us = 0;
    for (size_t i = 8; i < 16; ++i) {
        hdr.timestamp_us = (hdr.timestamp_us << 8) | body[i];
    }
    data = body + 16;
    if (len == 17) {
        // 新格式占位帧：16B 头 + 空 data + 1B flags（老对端 1 字节 data 的
        // 畸形帧同样落此分支，flags 非法时解码器按空帧丢弃，无副作用）
        data_len = 0;
        hdr.flags = body[16];
        return true;
    }
    // 老格式（16+data_len）恒 flags=0：兼容老对端
    hdr.flags = 0;
    data_len = len - 16;
    return true;
}

}  // namespace sc::localdesk::protocol
