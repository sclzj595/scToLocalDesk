// ============================================================
// scToLocalDesk | src/platform/qt_adapter/video_pipeline.hpp
// 控制端解码渲染管线（Doc18 §4.2，双端共用）：
//   EventBus(Evt_VideoFrame, 主线程) → BoundedQueue 只留最新 → 专属解码线程
//   → FfmpegDecoder/RAW 直通 → QImage → frameReady 信号（跨线程 queued 投递）
// QObject 线程亲和 = 创建线程（主线程），信号槽自动排队
// ============================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include <QImage>
#include <QObject>
#include <QSize>

#include "foundation/bounded_queue.hpp"

namespace sc::localdesk {

class FfmpegDecoder;  // implement/decoder（cpp 依 SCLD_HAS_FFMPEG include）

struct Evt_VideoFrame;  // core/events.hpp（platform 内引用需外层前向声明）

namespace foundation {
class EventBus;
class ILogger;
}

namespace platform {

class VideoPipeline final : public QObject {
    Q_OBJECT

public:
    VideoPipeline(std::shared_ptr<foundation::EventBus> bus,
                  std::shared_ptr<foundation::ILogger> logger,
                  QObject* parent = nullptr);
    ~VideoPipeline() override;

    void start();
    void stop();

    /// 解码 FPS（稳态值，设置页/诊断用）
    double fps() const { return fps_.load(std::memory_order_relaxed); }

    /// 最近一帧分辨率（键鼠归一化数据源，Doc18 §4.4）；无帧为 (0,0)
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY frameSizeChanged)
    QSize frameSize() const {
        return {static_cast<int>(frame_w_.load(std::memory_order_relaxed)),
                static_cast<int>(frame_h_.load(std::memory_order_relaxed))};
    }

signals:
    /// 解码完成（解码线程 emit；接收方在主线程 → 自动 queued）
    void frameReady(const QImage& img);
    /// 帧分辨率变更（解码线程 emit，queued）
    void frameSizeChanged();

private:
    /// EventBus 订阅回调（发布线程同步执行，仅入队必须快）
    void on_video_frame(const Evt_VideoFrame& e);

    void decode_loop();

    std::shared_ptr<foundation::EventBus> bus_;
    std::shared_ptr<foundation::ILogger> log_;
#if SCLD_HAS_FFMPEG && defined(_WIN32)
    std::unique_ptr<FfmpegDecoder> decoder_;  // Linux 解码器 P6 落位（FFmpeg 同构）
#endif

    struct Pending {
        std::shared_ptr<const std::vector<uint8_t>> payload;
        uint32_t width{0};
        uint32_t height{0};
    };
    foundation::BoundedQueue<Pending> queue_{3};  // 只留最新（画面最忌延迟累积）

    std::atomic<bool> running_{false};
    std::atomic<double> fps_{0.0};
    std::atomic<uint32_t> frame_w_{0};  ///< 最近帧宽（键鼠归一化用）
    std::atomic<uint32_t> frame_h_{0};
    std::thread decode_thread_;
};

}  // namespace platform
}  // namespace sc::localdesk
