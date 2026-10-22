/// TLV 协议编解码测试（Doc05 §1/§3）：头解析、整帧构建、信令体往返
#include <gtest/gtest.h>

#include <string>

#include "core/protocol/tlv.hpp"

using namespace sc::localdesk::protocol;

TEST(TlvHeader, RoundTrip) {
    std::vector<uint8_t> frame = build_frame(MsgType::ScreenFrame, {1, 2, 3, 4});
    ASSERT_EQ(frame.size(), kHeaderSize + 4);

    uint32_t body_len = 0;
    const auto type = parse_header(frame.data(), frame.size(), body_len);
    ASSERT_TRUE(type.has_value());
    EXPECT_EQ(*type, MsgType::ScreenFrame);
    EXPECT_EQ(body_len, 4u);
}

TEST(TlvHeader, IncompleteHeaderRejected) {
    const std::vector<uint8_t> short_buf{0x01, 0x00, 0x00};
    uint32_t body_len = 0;
    EXPECT_FALSE(parse_header(short_buf.data(), short_buf.size(), body_len).has_value());
}

TEST(TlvFrameComplete, PartialThenFull) {
    const std::vector<uint8_t> frame = build_frame(MsgType::FileChunk, {9, 9, 9});
    size_t total = 0;
    EXPECT_FALSE(is_frame_complete(frame.data(), frame.size() - 1, total));
    EXPECT_TRUE(is_frame_complete(frame.data(), frame.size(), total));
    EXPECT_EQ(total, frame.size());
}

TEST(TlvSignaling, RequestConnectRoundTrip) {
    const auto frame = build_request_connect("IT- workstation", connect_flags::kUnattended);
    uint32_t body_len = 0;
    const auto type = parse_header(frame.data(), frame.size(), body_len);
    ASSERT_TRUE(type.has_value());
    ASSERT_EQ(*type, MsgType::SessionCtrl);

    RequestConnectBody req;
    ASSERT_TRUE(parse_request_connect(frame.data() + kHeaderSize, body_len, req));
    EXPECT_EQ(req.alias, "IT- workstation");
    EXPECT_EQ(req.flags, connect_flags::kUnattended);
}

TEST(TlvSignaling, RejectBody) {
    const auto frame = build_reject(RejectReason::PasswordWrong);
    ASSERT_EQ(frame.size(), kHeaderSize + 2);
    EXPECT_EQ(frame[kHeaderSize], static_cast<uint8_t>(CtrlSubType::Reject));
    EXPECT_EQ(frame[kHeaderSize + 1], static_cast<uint8_t>(RejectReason::PasswordWrong));
}

TEST(TlvSignaling, MalformedRequestConnectRejected) {
    // alias_len=0 非法（Doc07 心跳字段校验同理）
    const std::vector<uint8_t> body{0x00, 0x00};
    RequestConnectBody req;
    EXPECT_FALSE(parse_request_connect(body.data(), body.size(), req));
}
