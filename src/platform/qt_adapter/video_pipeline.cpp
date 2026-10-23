// ============================================================
// scToLocalDesk | src/platform/qt_adapter/video_pipeline.cpp
// ============================================================
#include "platform/qt_adapter/video_pipeline.hpp"

#include <chrono>

#if SCLD_HAS_FFMPEG
    #include "implement/decoder/ffmpeg_decoder.hpp"
#endif

#include "core/events.hpp"
#include "foundation/event_bus.hpp"
#include "foundation/logger/ilogger.hpp"

namespace sc::localdesk::platform {

namespace {

using foundation::LogLevel;

bool is_jpeg(const std::vector<uint8_t>& d) {
    return d.size() >= 2 && d[0] == 0xFF && d[1] == 0xD8;
}

}  // namespace

VideoPipeline::VideoPipeline(std::shared_ptr<foundation::EventBus> bus,
                             std::shared_ptr<foundation::ILogger> logger,
                             QObject* parent)
    : QObject(parent),
      bus_(std::move(bus)),
      log_(std::move(logger)) {
#if SCLD_HAS_FFMPEG
    decoder_ = std::make_unique<FfmpegDecoder>();
#endif
    if (bus_ != nullptr) {
        bus_->subscribe<Evt_VideoFrame>(
            [this](const Evt_VideoFrame& e) { on_video_frame(e); });
    }
}

VideoPipeline::~VideoPipeline() {
    stop();
}

void VideoPipeline::start() {
    if (running_.load(std::memory_order_acquire)) {
        return;
    }
    running_.store(true, std::memory_order_release);
    decode_thread_ = std::thread(&VideoPipeline::decode_loop, this);
    SCLD_LOG(log_, LogLevel::Info, "video", "解码管线已启动");
}

void VideoPipeline::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    queue_.close();
    if (decode_thread_.joinable()) {
        decode_thread_.join();
    }
    queue_.reopen();
}

void VideoPipeline::on_video_frame(const Evt_VideoFrame& e) {
    if (!running_.load(std::memory_order_acquire) || e.payload == nullptr) {
        return;
    }
    Pending p;
    p.payload = e.payload;
    p.width = e.width;
    p.height = e.height;
    (void)queue_.push_overwrite(std::move(p));
}

void VideoPipeline::decode_loop() {
    // FPS 统计：每秒窗口滚动一次
    uint64_t decoded_count = 0;
    uint64_t drop_count = 0;
    auto window_start = std::chrono::steady_clock::now();

    Pending task;
    // pop_wait 超时返回 false ≠ 队列关闭：以 running_ 为循环条件，
    // 超时仅空转（此前 while(pop_wait) 写法使线程在无帧 200ms 后即退出）
    while (running_.load(std::memory_order_acquire)) {
        if (!queue_.pop_wait(task, 200)) {
            if (queue_.closed()) {
                break;
            }
            continue;
        }
        if (!running_.load(std::memory_order_acquire)) {
            break;
        }
        const auto& px = *task.payload;
        QImage img;
        if (is_jpeg(px)) {
#if SCLD_HAS_FFMPEG
            std::vector<uint8_t> bgra;
            uint32_t w = 0;
            uint32_t h = 0;
            if (decoder_ != nullptr
                && decoder_->decode(px.data(), px.size(), bgra, w, h) && !bgra.empty()) {
                // BGRA 逐行拷入 ARGB32（小端字节序一致；QImage 行对齐处理）
                img = QImage(static_cast<int>(w), static_cast<int>(h),
                             QImage::Format_ARGB32);
                const int row_bytes = static_cast<int>(w) * 4;
                for (uint32_t y = 0; y < h; ++y) {
                    std::memcpy(img.scanLine(static_cast<int>(y)), bgra.data()
                                    + static_cast<size_t>(y) * row_bytes,
                                static_cast<size_t>(row_bytes));
                }
            }
#endif
        }
        else if (task.width > 0 && task.height > 0
                 && px.size() >= static_cast<size_t>(task.width) * task.height * 4) {
            // RAW 调试直通（encoder=raw 兜底模式）：BGRA 原样入 QImage
            img = QImage(static_cast<int>(task.width), static_cast<int>(task.height),
                         QImage::Format_ARGB32);
            const int row_bytes = static_cast<int>(task.width) * 4;
            for (uint32_t y = 0; y < task.height; ++y) {
                std::memcpy(img.scanLine(static_cast<int>(y)),
                            px.data() + static_cast<size_t>(y) * row_bytes,
                            static_cast<size_t>(row_bytes));
            }
        }
        else {
            ++drop_count;
            continue;
        }

        if (img.isNull()) {
            ++drop_count;
            continue;
        }
        emit frameReady(img);
        ++decoded_count;

        const auto now = std::chrono::steady_clock::now();
        const double secs =
            std::chrono::duration<double>(now - window_start).count();
        if (secs >= 2.0) {
            fps_.store(static_cast<double>(decoded_count) / secs,
                       std::memory_order_relaxed);
            SCLD_LOG(log_, LogLevel::Info, "video",
                     "解码统计 fps=" + std::to_string(fps_.load(std::memory_order_relaxed))
                         + " drop=" + std::to_string(drop_count));
            decoded_count = 0;
            drop_count = 0;
            window_start = now;
        }
    }
}

}  // namespace sc::localdesk::platform
