// ============================================================
// scToLocalDesk | src/implement/decoder/ffmpeg_decoder.hpp
// FFmpeg MJPEG 解码器（与 FfmpegEncoder 同栈对称，SCLD_HAS_FFMPEG 才编入）
// 输出 packed BGRA（行连续，stride = w*4）；buffer 复用避免每帧分配
// ============================================================
#pragma once

#include <cstdint>
#include <vector>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace sc::localdesk {

class FfmpegDecoder final {
public:
    FfmpegDecoder() = default;
    ~FfmpegDecoder();

    FfmpegDecoder(const FfmpegDecoder&) = delete;
    FfmpegDecoder& operator=(const FfmpegDecoder&) = delete;

    /// JPEG → BGRA（输出尺寸以码流为准；分辨率变更自动重建）
    bool decode(const uint8_t* data, size_t len, std::vector<uint8_t>& bgra,
                uint32_t& width, uint32_t& height);

private:
    AVCodecContext* ctx_{nullptr};
    AVPacket* pkt_{nullptr};
    AVFrame* frame_{nullptr};
    SwsContext* sws_{nullptr};
    std::vector<uint8_t> out_;  // packed BGRA，跨帧复用
    uint32_t width_{0};
    uint32_t height_{0};
};

}  // namespace sc::localdesk
