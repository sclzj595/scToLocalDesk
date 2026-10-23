// ============================================================
// scToLocalDesk | src/implement/decoder/ffmpeg_decoder.cpp
// ============================================================
#include "implement/decoder/ffmpeg_decoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace sc::localdesk {

namespace {

bool is_jpeg(const uint8_t* d, size_t len) {
    return len >= 2 && d[0] == 0xFF && d[1] == 0xD8;  // JPEG SOI
}

}  // namespace

FfmpegDecoder::~FfmpegDecoder() {
    if (sws_ != nullptr) {
        sws_freeContext(sws_);
    }
    if (ctx_ != nullptr) {
        avcodec_free_context(&ctx_);
    }
}

bool FfmpegDecoder::decode(const uint8_t* data, size_t len, std::vector<uint8_t>& bgra,
                           uint32_t& width, uint32_t& height) {
    bgra.clear();
    width = 0;
    height = 0;
    if (data == nullptr || len == 0 || !is_jpeg(data, len)) {
        return false;  // RAW BGRA 帧不走解码器（调用方直接按 w*h*4 取用）
    }

    // 解码上下文懒建（mjpeg 解码器无参数依赖）
    if (ctx_ == nullptr) {
        const AVCodec* dec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
        if (dec == nullptr) {
            return false;
        }
        ctx_ = avcodec_alloc_context3(dec);
        if (ctx_ == nullptr) {
            return false;
        }
        if (avcodec_open2(ctx_, dec, nullptr) < 0) {
            avcodec_free_context(&ctx_);
            return false;
        }
        pkt_ = av_packet_alloc();
        frame_ = av_frame_alloc();
        if (pkt_ == nullptr || frame_ == nullptr) {
            return false;
        }
    }

    pkt_->data = const_cast<uint8_t*>(data);  // 不拥有： packet 复用仅换指针
    pkt_->size = static_cast<int>(len);
    const bool send_ok = avcodec_send_packet(ctx_, pkt_) >= 0;
    pkt_->data = nullptr;  // 复用前解除引用（避免 free 语义歧义）
    pkt_->size = 0;
    if (!send_ok) {
        return false;
    }

    bool ok = false;
    const int rc = avcodec_receive_frame(ctx_, frame_);
    if (rc == 0) {
        width = static_cast<uint32_t>(frame_->width);
        height = static_cast<uint32_t>(frame_->height);
        // 分辨率变更：重建 sws + 输出 buffer
        if (width != width_ || height != height_) {
            if (sws_ != nullptr) {
                sws_freeContext(sws_);
                sws_ = nullptr;
            }
            out_.clear();
        }
        if (sws_ == nullptr) {
            sws_ = sws_getContext(frame_->width, frame_->height,
                                  static_cast<AVPixelFormat>(frame_->format),
                                  static_cast<int>(width), static_cast<int>(height),
                                  AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr,
                                  nullptr);
            if (sws_ != nullptr) {
                width_ = width;
                height_ = height;
                out_.resize(static_cast<size_t>(width_) * height_ * 4);
            }
        }
        if (sws_ != nullptr && !out_.empty()) {
            const uint8_t* src[4] = {frame_->data[0], frame_->data[1], frame_->data[2],
                                     nullptr};
            const int src_stride[4] = {frame_->linesize[0], frame_->linesize[1],
                                       frame_->linesize[2], 0};
            uint8_t* dst[4] = {out_.data(), nullptr, nullptr, nullptr};
            const int dst_stride[4] = {static_cast<int>(width_) * 4, 0, 0, 0};
            if (sws_scale(sws_, src, src_stride, 0, frame_->height, dst, dst_stride)
                == frame_->height) {
                bgra = out_;  // 拷贝出参（成员复用减少分配）
                ok = true;
            }
        }
    }
    av_frame_unref(frame_);
    return ok;
}

}  // namespace sc::localdesk
