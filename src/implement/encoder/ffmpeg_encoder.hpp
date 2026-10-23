// ============================================================
// scToLocalDesk | src/implement/encoder/ffmpeg_encoder.hpp
// FFmpeg 编码器（avcodec 动态链接 LGPL，SCLD_HAS_FFMPEG 才编入）
// MJPEG 起步（帧间独立，无 GOP 复杂度）；H264 为后续增量
// sws 承担 BGRA→YUVJ420P（SIMD），质量映射 AppConfig.jpeg_quality
// ============================================================
#pragma once

#include <cstdint>
#include <vector>

#include "core/interface/iencoder.hpp"

struct AVCodecContext;
struct AVFrame;
struct SwsContext;

namespace sc::localdesk {

class FfmpegEncoder final : public IEncoder {
public:
    FfmpegEncoder() = default;
    ~FfmpegEncoder() override;

    FfmpegEncoder(const FfmpegEncoder&) = delete;
    FfmpegEncoder& operator=(const FfmpegEncoder&) = delete;

    bool open(const EncoderConfig& cfg) override;
    bool encode(const VideoFrame& in, std::vector<uint8_t>& out) override;
    void close() override;

private:
    bool init_frame(uint32_t width, uint32_t height);  // 首帧懒初始化/分辨率变更重建

    EncoderConfig cfg_;
    AVCodecContext* ctx_{nullptr};
    SwsContext* sws_{nullptr};
    AVFrame* frame_{nullptr};
    uint32_t width_{0};
    uint32_t height_{0};
    int64_t pts_{0};
};

}  // namespace sc::localdesk
