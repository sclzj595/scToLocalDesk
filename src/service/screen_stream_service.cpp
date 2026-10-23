// ============================================================
// scToLocalDesk | src/service/screen_stream_service.cpp
// 推流管线实现（Doc10 §2/§3：丢帧策略 + StreamLag 事件）
// ============================================================
#include "service/screen_stream_service.hpp"

#include <chrono>

#include "core/events.hpp"
#include "core/interface/iencoder.hpp"
#include "core/interface/iscreen_capturer.hpp"
#include "core/protocol/tlv.hpp"
#include "foundation/event_bus.hpp"
#include "foundation/logger/ilogger.hpp"
#include "foundation/perf_counter.hpp"

namespace sc::localdesk {

namespace {

using sc::localdesk::foundation::LogLevel;

int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

ScreenStreamService::ScreenStreamService(std::shared_ptr<foundation::EventBus> bus,
                                         std::shared_ptr<foundation::ILogger> logger)
    : bus_(std::move(bus)),
      log_(std::move(logger)) {}

ScreenStreamService::~ScreenStreamService() {
    stop();
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
    if (send_thread_.joinable()) {
        send_thread_.join();
    }
}

void ScreenStreamService::set_source(std::shared_ptr<IScreenCapturer> capturer,
                                     std::shared_ptr<IEncoder> encoder) {
    capturer_ = std::move(capturer);
    encoder_ = std::move(encoder);
}

bool ScreenStreamService::start(SendFn send, const Options& opt) {
    if (!send || capturer_ == nullptr || encoder_ == nullptr) {
        SCLD_LOG(log_, LogLevel::Error, "stream", "推流启动失败：send/capturer/encoder 未就绪");
        return false;
    }
    stop();
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
    if (send_thread_.joinable()) {
        send_thread_.join();
    }
    queue_.reopen();
    queue_.clear();
    dropped_total_.store(0, std::memory_order_relaxed);
    running_.store(true, std::memory_order_release);
    // SendFn 值拷贝两份（capture_loop 不发送但签名保留；此前 std::move 后
    // send_thread 拿到空 function，首帧 bad_function_call → terminate）
    capture_thread_ = std::thread(&ScreenStreamService::capture_loop, this, send, opt);
    send_thread_ = std::thread([this, send] { send_loop(send); });
    SCLD_LOG(log_, LogLevel::Info, "stream",
             "推流已启动 fps=" + std::to_string(opt.fps));
    return true;
}

void ScreenStreamService::stop() {
    running_.store(false, std::memory_order_release);
    queue_.close();  // 唤醒发送线程；非阻塞（可持会话锁调用）
}

void ScreenStreamService::capture_loop(SendFn send, Options opt) {
    (void)send;
    if (!capturer_->init()) {
        SCLD_LOG(log_, LogLevel::Error, "stream", "采集器初始化失败，推流终止");
        running_.store(false, std::memory_order_release);
        queue_.close();
        return;
    }
    if (!encoder_->open(EncoderConfig{"mjpeg", opt.fps, opt.quality})) {
        SCLD_LOG(log_, LogLevel::Error, "stream", "编码器打开失败，推流终止");
        capturer_->shutdown();
        running_.store(false, std::memory_order_release);
        queue_.close();
        return;
    }

    const auto interval = std::chrono::milliseconds(1000 / (opt.fps > 0 ? opt.fps : 15));
    const double budget_ms = static_cast<double>(interval.count());
    auto next_tick = std::chrono::steady_clock::now();
    VideoFrame frame;

    // 采集+编码耗时可见性（perf_counter：吸收 NetComm MicroCounter 统计口径）
    foundation::PerfCounter frame_perf;
    constexpr uint64_t kPerfReportEvery = 300;   // 每 300 帧上报一次均值/峰值

    while (running_.load(std::memory_order_acquire)) {
        next_tick += interval;
        {
            foundation::ScopedPerf sp(frame_perf);   // grab+encode 全程计时
            if (capturer_->grab(frame)) {
                std::vector<uint8_t> encoded;
                if (encoder_->encode(frame, encoded) && !encoded.empty()) {
                    protocol::ScreenFrameHeader hdr;
                    hdr.width = frame.width;
                    hdr.height = frame.height;
                    hdr.timestamp_us = frame.timestamp_us != 0 ? frame.timestamp_us : now_us();
                    auto pkt = protocol::build_screen_frame(hdr, encoded.data(), encoded.size());
                    dropped_total_.fetch_add(queue_.push_overwrite(std::move(pkt)),
                                             std::memory_order_relaxed);
                }
            }
        }
        // 帧预算超支：单帧告警（编码器/采集器性能退化的第一信号）
        if (frame_perf.last_ms() > budget_ms) {
            SCLD_LOG(log_, LogLevel::Warn, "stream",
                     "帧处理超预算 last=" + std::to_string(frame_perf.last_ms())
                         + "ms budget=" + std::to_string(budget_ms) + "ms");
        }
        // 周期统计上报：均值/峰值/最大吞吐参考
        if (frame_perf.count() >= kPerfReportEvery) {
            SCLD_LOG(log_, LogLevel::Info, "stream",
                     "帧耗时统计 n=" + std::to_string(frame_perf.count())
                         + " avg=" + std::to_string(frame_perf.avg_ms())
                         + "ms max=" + std::to_string(frame_perf.max_ms()) + "ms");
            frame_perf.reset();
        }
        // 丢帧聚合上报（Doc10 §3.2：连续丢弃上抛 StreamLag）
        if (dropped_total_.load(std::memory_order_relaxed) >= 100) {
            if (bus_ != nullptr) {
                Evt_StreamLag e;
                e.dropped_frames = dropped_total_.exchange(0, std::memory_order_relaxed);
                e.timestamp_us = now_us();
                bus_->publish(e);
            }
        }
        std::this_thread::sleep_until(next_tick);
    }

    encoder_->close();
    capturer_->shutdown();
}

void ScreenStreamService::send_loop(SendFn send) {
    std::vector<uint8_t> pkt;
    while (queue_.pop_wait(pkt, 200)) {
        if (!running_.load(std::memory_order_acquire)) {
            break;
        }
        if (!send(pkt)) {
            break;  // 会话已关闭（send_business_frame 返回 false）
        }
    }
}

}  // namespace sc::localdesk
