// ============================================================
// scToLocalDesk | src/core/interface/iencoder.hpp
// 图像编码抽象契约：MJPG 默认 / H264 可选 FFmpeg（Doc06 §3 / Doc10）
// ============================================================
#pragma once

#include <string>
#include <vector>

#include "core/interface/types.hpp"

namespace sc::localdesk {

struct EncoderConfig {
    std::string codec{"mjpeg"};  ///< mjpeg | h264
    int fps{15};                 ///< 10 / 15 / 24 / 30
    int quality{80};             ///< MJPG 质量 1~100；H264 映射 CRP
};

class IEncoder {
public:
    virtual ~IEncoder() = default;

    [[nodiscard]] virtual bool open(const EncoderConfig& cfg) = 0;

    /// 输入一帧原始图，输出编码字节流（编码线程专用）
    virtual bool encode(const VideoFrame& in, std::vector<uint8_t>& out) = 0;

    virtual void close() = 0;
};

}  // namespace sc::localdesk
