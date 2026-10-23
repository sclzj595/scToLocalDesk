// ============================================================
// scToLocalDesk | src/implement/encoder/ffmpeg_encoder.cpp
// ============================================================
#include "implement/encoder/ffmpeg_encoder.hpp"

#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace sc::localdesk {

namespace {

// quality 1~100 → qscale 2~31（JPEG 量化步，反向：质高步小）
int quality_to_qscale(int quality) {
    if (quality < 1) {
        quality = 1;
    }
    if (quality > 100) {
        quality = 100;
    }
    return 2 + (100 - quality) * 29 / 100;
}

}  // namespace

FfmpegEncoder::~FfmpegEncoder() {
    close();
}

bool FfmpegEncoder::open(const EncoderConfig& cfg) {
    if (cfg.codec != "mjpeg") {
        return false;  // 暂只支持 mjpeg（H264 为后续增量）；失败由上层日志
    }
    cfg_ = cfg;
    // 帧尺寸在首帧才知道：ctx 于 init_frame 懒建（IEncoder 契约无宽高参数）
    return true;
}

bool FfmpegEncoder::init_frame(uint32_t width, uint32_t height) {
    close();
    // YUV420 要求偶数尺寸
    width_ = width & ~1u;
    height_ = height & ~1u;

    const AVCodec* enc = avcodec_find_encoder_by_name(cfg_.codec.c_str());
    if (enc == nullptr) {
        return false;
    }
    ctx_ = avcodec_alloc_context3(enc);
    if (ctx_ == nullptr) {
        return false;
    }
    ctx_->width = static_cast<int>(width_);
    ctx_->height = static_cast<int>(height_);
    ctx_->pix_fmt = AV_PIX_FMT_YUVJ420P;
    ctx_->time_base = AVRational{1, cfg_.fps > 0 ? cfg_.fps : 15};
    ctx_->flags |= AV_CODEC_FLAG_QSCALE;
    ctx_->global_quality = FF_QP2LAMBDA * quality_to_qscale(cfg_.quality);
    if (avcodec_open2(ctx_, enc, nullptr) < 0) {
        close();
        return false;
    }

    sws_ = sws_getContext(static_cast<int>(width), static_cast<int>(height),
                          AV_PIX_FMT_BGRA, static_cast<int>(width_),
                          static_cast<int>(height_), AV_PIX_FMT_YUVJ420P,
                          SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (sws_ == nullptr) {
        close();
        return false;
    }

    frame_ = av_frame_alloc();
    if (frame_ == nullptr) {
        close();
        return false;
    }
    frame_->format = AV_PIX_FMT_YUVJ420P;
    frame_->width = static_cast<int>(width_);
    frame_->height = static_cast<int>(height_);
    if (av_frame_get_buffer(frame_, 32) < 0) {
        close();
        return false;
    }
    return true;
}

bool FfmpegEncoder::encode(const VideoFrame& in, std::vector<uint8_t>& out) {
    out.clear();
    if (in.data == nullptr || in.width == 0 || in.height == 0) {
        return false;
    }
    // 分辨率变更（显示器切换）：重建管线
    if (ctx_ == nullptr || static_cast<uint32_t>(ctx_->width) != (in.width & ~1u)
        || static_cast<uint32_t>(ctx_->height) != (in.height & ~1u)) {
        if (!init_frame(in.width, in.height)) {
            return false;
        }
    }

    // GDI top-down DIB：pitch = width*4，逐行送 sws（GDI pitch 天然 4 对齐）
    const uint8_t* src[1] = {in.data};
    const int src_stride[1] = {static_cast<int>(in.width) * 4};
    uint8_t* dst[4] = {frame_->data[0], frame_->data[1], frame_->data[2], nullptr};
    const int dst_stride[4] = {frame_->linesize[0], frame_->linesize[1],
                               frame_->linesize[2], 0};
    sws_scale(sws_, src, src_stride, 0, static_cast<int>(in.height), dst,
              dst_stride);

    frame_->pts = pts_++;
    if (avcodec_send_frame(ctx_, frame_) < 0) {
        return false;
    }
    AVPacket* pkt = av_packet_alloc();
    if (pkt == nullptr) {
        return false;
    }
    bool ok = true;
    while (true) {
        const int rc = avcodec_receive_packet(ctx_, pkt);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
            break;
        }
        if (rc < 0) {
            ok = false;
            break;
        }
        out.insert(out.end(), pkt->data, pkt->data + pkt->size);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return ok && !out.empty();
}

void FfmpegEncoder::close() {
    if (frame_ != nullptr) {
        av_frame_free(&frame_);
    }
    if (sws_ != nullptr) {
        sws_freeContext(sws_);
        sws_ = nullptr;
    }
    if (ctx_ != nullptr) {
        avcodec_free_context(&ctx_);
    }
    width_ = 0;
    height_ = 0;
    pts_ = 0;
}

}  // namespace sc::localdesk
