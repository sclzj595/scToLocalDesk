/// Buffer 单测：大端序读写、游标越界、占位回写
#include <gtest/gtest.h>

#include "foundation/buffer.hpp"

using sc::localdesk::foundation::Buffer;

TEST(Buffer, WriteReadBigEndian) {
    Buffer b;
    b.append_u8(0x01);
    b.append_u16be(0x1234);
    b.append_u32be(0xDEADBEEF);

    uint8_t u8 = 0;
    uint16_t u16 = 0;
    uint32_t u32 = 0;
    ASSERT_TRUE(b.read_u8(u8));
    ASSERT_TRUE(b.read_u16be(u16));
    ASSERT_TRUE(b.read_u32be(u32));
    EXPECT_EQ(u8, 0x01);
    EXPECT_EQ(u16, 0x1234);
    EXPECT_EQ(u32, 0xDEADBEEF);
    EXPECT_EQ(b.readable(), 0u);
}

TEST(Buffer, ReadBeyondEndFailsWithoutMovingCursor) {
    Buffer b;
    b.append_u8(0x42);
    uint16_t u16 = 0;
    EXPECT_FALSE(b.read_u16be(u16));  // 不足 2 字节
    uint8_t u8 = 0;
    ASSERT_TRUE(b.read_u8(u8));       // 游标未被破坏
    EXPECT_EQ(u8, 0x42);
}

TEST(Buffer, String8RoundTrip) {
    Buffer b;
    b.append_string8("小王-01");
    std::string out;
    ASSERT_TRUE(b.read_string8(out));
    EXPECT_EQ(out, "小王-01");
}

TEST(Buffer, OverwriteU32ForHeaderPatching) {
    Buffer b;
    b.append_u32be(0);
    b.overwrite_u32be(0, 0x11223344);
    uint32_t v = 0;
    b.reset_read();
    ASSERT_TRUE(b.read_u32be(v));
    EXPECT_EQ(v, 0x11223344u);
}

TEST(Buffer, TakeTransfersOwnership) {
    Buffer b;
    b.append_u32be(7);
    const auto raw = b.take();
    EXPECT_EQ(raw.size(), 4u);
    EXPECT_TRUE(b.empty());
}
