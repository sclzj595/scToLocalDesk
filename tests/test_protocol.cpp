// ============================================================
// scToLocalDesk | tests/test_protocol.cpp
// TLV 协议编解码用例（Doc05）：整帧构建/解析、半包粘包判定、
// 各信令往返。契约：builder 返回整帧（5B 头+body），parser 吃裸 body
// ============================================================
#include "doctest.h"

#include "core/protocol/tlv.hpp"
#include "core/interface/types.hpp"  // kFileFlagIsDir（Doc19 §5 目录传输位）

using namespace sc::localdesk;
using namespace sc::localdesk::protocol;

/// 取整帧的裸 body 视图（builder 契约：返回整帧）
static std::vector<uint8_t> frame_body(const std::vector<uint8_t>& frame) {
    REQUIRE(frame.size() >= kHeaderSize);
    return std::vector<uint8_t>(frame.begin() + kHeaderSize, frame.end());
}

TEST_CASE("整帧构建与头解析往返")
{
    const std::vector<uint8_t> body = {0x11, 0x22, 0x33};
    const auto frame = build_frame(MsgType::FileMeta, body);

    REQUIRE(frame.size() == kHeaderSize + body.size());

    uint32_t body_len = 0;
    MsgType type;
    REQUIRE(parse_header(frame.data(), frame.size(), body_len, type));
    CHECK(type == MsgType::FileMeta);
    CHECK(body_len == 3);
}

TEST_CASE("半包与粘包判定")
{
    const std::vector<uint8_t> body = {0xAA, 0xBB};
    const auto frame = build_frame(MsgType::SessionCtrl, body);

    size_t total = 0;
    // 半包：头未齐 / body 未齐均不完整
    CHECK_FALSE(is_frame_complete(frame.data(), 2, total));
    CHECK_FALSE(is_frame_complete(frame.data(), frame.size() - 1, total));
    // 整帧到位
    CHECK(is_frame_complete(frame.data(), frame.size(), total));
    CHECK(total == frame.size());

    // 粘包：两帧连排，按第一帧 total 可切出第二帧
    std::vector<uint8_t> sticky = frame;
    sticky.insert(sticky.end(), frame.begin(), frame.end());
    REQUIRE(is_frame_complete(sticky.data(), sticky.size(), total));
    CHECK(total == frame.size());
    uint32_t body_len = 0;
    MsgType second;
    REQUIRE(parse_header(sticky.data() + total, sticky.size() - total,
                         body_len, second));
    CHECK(second == MsgType::SessionCtrl);
    CHECK(body_len == 2);
}

TEST_CASE("头长度不足返回 false")
{
    uint32_t body_len = 0;
    MsgType type;
    const uint8_t one = 0x04;
    CHECK_FALSE(parse_header(&one, 1, body_len, type));
}

TEST_CASE("REQUEST_CONNECT 往返")
{
    const auto frame = build_request_connect("SC-PC", connect_flags::kUnattended);
    uint32_t body_len = 0;
    MsgType type;
    REQUIRE(parse_header(frame.data(), frame.size(), body_len, type));
    CHECK(type == MsgType::SessionCtrl);
    const auto body = frame_body(frame);

    RequestConnectBody out;
    REQUIRE(parse_request_connect(body.data(), body.size(), out));
    CHECK(out.alias == "SC-PC");
    CHECK(out.flags == connect_flags::kUnattended);
    // 版本协商（SessionCtrl 首包）：新格式末尾恒带 1B 版本
    CHECK(out.version == kProtoVersion);
}

TEST_CASE("REQUEST_CONNECT 老格式兼容（无版本字节按 0）")
{
    // 手工拼老格式 body：sub + alias_len + alias + flags（无末尾版本字节），
    // 模拟老对端发来的请求——解析须成功且 version==0（legacy 放行）
    const std::string alias = "OLD-PC";
    std::vector<uint8_t> body;
    body.push_back(static_cast<uint8_t>(CtrlSubType::RequestConnect));
    body.push_back(static_cast<uint8_t>(alias.size()));
    body.insert(body.end(), alias.begin(), alias.end());
    body.push_back(0x00);  // flags

    RequestConnectBody out;
    REQUIRE(parse_request_connect(body.data(), body.size(), out));
    CHECK(out.alias == alias);
    CHECK(out.flags == 0x00);
    CHECK(out.version == 0);
}

TEST_CASE("REQUEST_CONNECT 越界 body 解析拒绝")
{
    RequestConnectBody out;
    CHECK_FALSE(parse_request_connect(nullptr, 0, out));
    const uint8_t junk[] = {0xFF};
    CHECK_FALSE(parse_request_connect(junk, 1, out));
}

TEST_CASE("FILE_META 往返（含中文名）")
{
    // file_id 契约：32 hex 字符 = 16B（生产链路由 16B 随机数 hex 化而来）
    const auto frame = build_file_meta("0123456789abcdef0123456789abcdef",
                                        "测试文件.bin", 1234567890ULL, 32);
    uint32_t body_len = 0;
    MsgType type;
    REQUIRE(parse_header(frame.data(), frame.size(), body_len, type));
    CHECK(type == MsgType::FileMeta);
    const auto body = frame_body(frame);

    FileMetaBody out;
    REQUIRE(parse_file_meta(body.data(), body.size(), out));
    CHECK(out.file_id == "0123456789abcdef0123456789abcdef");
    CHECK(out.name == "测试文件.bin");
    CHECK(out.size == 1234567890ULL);
    CHECK(out.total_chunks == 32);
    CHECK(out.flags == 0);  // 老格式（flags=0 不写字节）往返恒 0
}

TEST_CASE("FILE_META flags 往返（Doc19 §5 目录传输扩展）")
{
    // flags≠0：末尾追加 1B，按 len 判别解析
    const auto frame = build_file_meta("0123456789abcdef0123456789abcdef",
                                        "docs", 0, 0, kFileFlagIsDir);
    const auto body = frame_body(frame);
    FileMetaBody out;
    REQUIRE(parse_file_meta(body.data(), body.size(), out));
    CHECK(out.name == "docs");
    CHECK(out.size == 0);
    CHECK(out.total_chunks == 0);
    CHECK(out.flags == kFileFlagIsDir);

    // flags=0：字节布局与老格式完全一致（无附加字节）
    const auto frame_old = build_file_meta("0123456789abcdef0123456789abcdef",
                                           "a.bin", 8, 1);
    const auto body_old = frame_body(frame_old);
    FileMetaBody out_old;
    REQUIRE(parse_file_meta(body_old.data(), body_old.size(), out_old));
    CHECK(out_old.flags == 0);
    CHECK(body_old.size() == 16 + 2 + 5 + 8 + 4);  // 基础布局长度
}

TEST_CASE("FILE_META 截断 body 解析拒绝")
{
    const auto body = frame_body(build_file_meta("0123456789abcdef0123456789abcdef",
                                                 "a.bin", 1, 1));
    FileMetaBody out;
    // 掐掉尾部字段
    CHECK_FALSE(parse_file_meta(body.data(), body.size() - 4, out));
}

TEST_CASE("键鼠帧往返")
{
    SUBCASE("鼠标移动 + 滚轮")
    {
        MouseKeyBody mk;
        mk.kind = MouseKeyKind::Mouse;
        mk.action = 1;
        mk.x = 32768;
        mk.y = 65535;
        mk.wheel = -120;
        const auto body = frame_body(build_mouse_key(mk));
        MouseKeyBody out;
        REQUIRE(parse_mouse_key(body.data(), body.size(), out));
        CHECK(out.kind == MouseKeyKind::Mouse);
        CHECK(out.action == 1);
        CHECK(out.x == 32768);
        CHECK(out.y == 65535);
        CHECK(out.wheel == -120);
    }
    SUBCASE("键盘按键")
    {
        MouseKeyBody mk;
        mk.kind = MouseKeyKind::Keyboard;
        mk.vk = 0x41;
        mk.scan = 0x1E;
        mk.down = 1;
        const auto body = frame_body(build_mouse_key(mk));
        MouseKeyBody out;
        REQUIRE(parse_mouse_key(body.data(), body.size(), out));
        CHECK(out.kind == MouseKeyKind::Keyboard);
        CHECK(out.vk == 0x41);
        CHECK(out.scan == 0x1E);
        CHECK(out.down == 1);
    }
}

TEST_CASE("剪贴板帧往返与超限拒绝")
{
    // 常规往返（UTF-8 中文）
    const std::string text = "scld 剪贴板同步 abc123";
    const auto frame = build_clipboard_sync(text);
    REQUIRE(frame.size() == protocol::kHeaderSize + 4 + text.size());
    const auto body = frame_body(frame);
    std::string out;
    REQUIRE(parse_clipboard_sync(body.data(), body.size(), out));
    CHECK(out == text);

    // 空文本：构建合法（len=0），解析得空串
    const auto empty_frame = build_clipboard_sync("");
    const auto empty_body = frame_body(empty_frame);
    REQUIRE(parse_clipboard_sync(empty_body.data(), empty_body.size(), out));
    CHECK(out.empty());

    // 截断 body：声明长度大于实际 → 拒绝
    CHECK_FALSE(parse_clipboard_sync(body.data(), body.size() - 1, out));

    // body 短于 4B 头 → 拒绝
    const uint8_t tiny[] = {0, 0};
    CHECK_FALSE(parse_clipboard_sync(tiny, sizeof(tiny), out));

    // 超限文本：构建即拒（空帧）
    const std::string big(kClipboardMaxBytes + 1, 'x');
    CHECK(build_clipboard_sync(big).empty());
}
TEST_CASE("KEY_SYNC 密钥协商帧往返")
{
    std::vector<uint8_t> pub(64), nonce(16);
    for (size_t i = 0; i < pub.size(); ++i) {
        pub[i] = static_cast<uint8_t>(i);
    }
    for (size_t i = 0; i < nonce.size(); ++i) {
        nonce[i] = static_cast<uint8_t>(0xF0 + i);
    }
    const auto frame = build_key_sync(pub.data(), nonce.data());
    REQUIRE(frame.size() == kHeaderSize + 80);

    uint32_t body_len = 0;
    MsgType type;
    REQUIRE(parse_header(frame.data(), frame.size(), body_len, type));
    CHECK(type == MsgType::KeySync);
    CHECK(body_len == 80);

    std::vector<uint8_t> pub_out, nonce_out;
    REQUIRE(parse_key_sync(frame.data() + kHeaderSize, body_len, pub_out, nonce_out));
    CHECK(pub_out == pub);
    CHECK(nonce_out == nonce);

    // 长度不符 / 空指针拒绝
    CHECK_FALSE(parse_key_sync(frame.data() + kHeaderSize, 79, pub_out, nonce_out));
    CHECK(build_key_sync(nullptr, nonce.data()).empty());
}

TEST_CASE("画面帧零拷贝解析")
{
    const uint8_t pixels[] = {1, 2, 3, 4, 5};
    ScreenFrameHeader hdr;
    hdr.width = 1920;
    hdr.height = 1080;
    hdr.timestamp_us = 1234567890123LL;
    const auto body = frame_body(build_screen_frame(hdr, pixels, sizeof(pixels)));

    ScreenFrameHeader out;
    const uint8_t* data = nullptr;
    size_t data_len = 0;
    REQUIRE(parse_screen_frame(body.data(), body.size(), out, data, data_len));
    CHECK(out.width == 1920);
    CHECK(out.height == 1080);
    CHECK(out.timestamp_us == 1234567890123LL);
    REQUIRE(data_len == sizeof(pixels));
    // 零拷贝：数据视图直指 body 缓冲
    CHECK(data == body.data() + 16);
    CHECK(data[0] == 1);
    CHECK(data[4] == 5);
}

TEST_CASE("控制报文整帧：头 + sub(+reason)")
{
    // builder 契约：返回 SESSION_CTRL 整帧，body 首字节恒为 sub（Doc05 §3）
    const auto ok = build_ctrl(CtrlSubType::Approved);
    CHECK(ok.size() == kHeaderSize + 1);
    CHECK(ok[kHeaderSize] == static_cast<uint8_t>(CtrlSubType::Approved));

    // 带附加字节版（P1 只读告知）：Approved 尾随 1B（0=可控制 1=只读）；
    // 旧解析按单字节 Approved 处理、多余字节忽略——双向兼容
    const auto ok_ro = build_ctrl(CtrlSubType::Approved, 1);
    CHECK(ok_ro.size() == kHeaderSize + 2);
    CHECK(ok_ro[kHeaderSize] == static_cast<uint8_t>(CtrlSubType::Approved));
    CHECK(ok_ro[kHeaderSize + 1] == 1);

    const auto rej = build_reject(RejectReason::SessionBusy);
    CHECK(rej.size() == kHeaderSize + 2);
    CHECK(rej[kHeaderSize] == static_cast<uint8_t>(CtrlSubType::Reject));
    CHECK(rej[kHeaderSize + 1] == static_cast<uint8_t>(RejectReason::SessionBusy));

    const auto cls = build_close(CloseReason::Normal);
    CHECK(cls.size() == kHeaderSize + 2);
    CHECK(cls[kHeaderSize] == static_cast<uint8_t>(CtrlSubType::Close));
    CHECK(cls[kHeaderSize + 1] == static_cast<uint8_t>(CloseReason::Normal));
}
