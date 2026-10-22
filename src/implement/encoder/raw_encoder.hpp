// ============================================================
// scToLocalDesk | src/implement/encoder/raw_encoder.hpp
// RAW 直通编码器（骨架期管线联调用）：BGRA 原样拷贝，不压缩
// MJPG（stb/libjpeg-turbo）与 H264（FFmpeg）为 V1.0 任务（Doc10 §1）
// 带宽预警：1080p@15fps RAW ≈ 124 MB/s，仅限内网千兆联调
// ============================================================
#pragma once

#include <cstring>
#include <vector>

#include "core/interface/iencoder.hpp"

namespace sc::localdesk {

class RawEncoder final : public IEncoder {
public:
    bool open(const EncoderConfig& cfg) override {
        cfg_ = cfg;
        opened_ = true;
        return true;
    }

    bool encode(const VideoFrame& in, std::vector<uint8_t>& out) override {
        if (!opened_ || in.data == nullptr || in.size == 0) {
            return false;
        }
        out.resize(in.size);
        std::memcpy(out.data(), in.data, in.size);
        return true;
    }

    void close() override { opened_ = false; }

private:
    EncoderConfig cfg_;
    bool opened_{false};
};

}  // namespace sc::localdesk
