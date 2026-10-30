// ============================================================
// scToLocalDesk | src/service/screen_stream_service.cpp
// 推流管线实现（Doc10 §2/§3：丢帧策略 + StreamLag 事件）
// ============================================================
#include "service/screen_stream_service.hpp"

#include <algorithm>
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

void ScreenStreamService::set_watermark(std::shared_ptr<std::vector<uint8_t>> bgra,
                                        int w, int h) {
    std::lock_guard<std::mutex> lk(wm_mtx_);
    wm_ = std::move(bgra);
    wm_w_ = w;
    wm_h_ = h;
    SCLD_LOG(log_, LogLevel::Info, "stream",
             wm_ != nullptr
                 ? "会话水印已启用 " + std::to_string(w) + "x" + std::to_string(h)
                 : "会话水印已清除");
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

    // 运行中档位（自适应画质 Doc19 §4.5 可调，初始=满档）
    int cur_fps = opt.fps > 0 ? opt.fps : 15;
    int cur_quality = std::min(opt.quality, 85);  // 满档上限 85（升档恢复口径一致）
    auto interval = std::chrono::milliseconds(1000 / cur_fps);
    const double budget_ms0 = static_cast<double>(interval.count());
    double budget_ms = budget_ms0;
    auto next_tick = std::chrono::steady_clock::now();
    VideoFrame frame;

    // —— 自适应画质闭环（Doc19 §4.5 P3）：5s 窗口两档，防高动态画面打满内网 ——
    // 降档判据：窗口丢帧率 >20% 或队列 ≥80% tick 常满 → quality-15（下限40）+
    // fps min(原,10)；升档判据：连续 3 窗口（15s）丢帧率 <2% 且不常满 → 恢复
    // 满档。两档设计每次一档天然满足"间隔 ≥5s 缓慢恢复"防振荡
    int cur_tier = 0;              // 0=满档 1=降档
    constexpr double kDropHigh = 0.20;
    constexpr double kDropLow = 0.02;
    int win_produced = 0;          // 窗口内成功编码帧数
    int win_dropped = 0;           // 窗口内被队列挤掉的帧数
    int win_ticks = 0;             // 窗口内采集 tick 数
    int win_full_ticks = 0;        // 窗口内队列满 tick 数（常满判据）
    int good_windows = 0;          // 连续低丢帧窗口数（升档判据）
    auto win_start = std::chrono::steady_clock::now();
    // 档位切换：改间隔/预算 + 重锚 next_tick（防降档后追帧连发）+ 编码器热调
    auto apply_tier = [&](int tier) {
        if (tier == cur_tier) {
            return;
        }
        cur_tier = tier;
        if (tier == 1) {
            cur_quality = std::max(40, std::min(opt.quality, 85) - 15);
            cur_fps = std::min(cur_fps, 10);
            SCLD_LOG(log_, LogLevel::Info, "stream",
                     "自适应降档 quality=" + std::to_string(cur_quality)
                         + " fps=" + std::to_string(cur_fps)
                         + "（丢帧率高或队列常满，压低码率保流畅）");
        }
        else {
            cur_quality = std::min(opt.quality, 85);
            cur_fps = opt.fps > 0 ? opt.fps : 15;
            SCLD_LOG(log_, LogLevel::Info, "stream",
                     "自适应升档恢复 quality=" + std::to_string(cur_quality)
                         + " fps=" + std::to_string(cur_fps)
                         + "（链路持续空闲）");
        }
        interval = std::chrono::milliseconds(1000 / cur_fps);
        budget_ms = static_cast<double>(interval.count());
        next_tick = std::chrono::steady_clock::now() + interval;
        encoder_->set_quality(cur_quality);
    };

    // 采集+编码耗时可见性（perf_counter：吸收 NetComm MicroCounter 统计口径）
    foundation::PerfCounter frame_perf;
    constexpr uint64_t kPerfReportEvery = 300;   // 每 300 帧上报一次均值/峰值

    // 采集失败占位（Doc19 §4.2）：连续失败 ≥2s 且 last_error 有分类时改推
    // 占位帧（每 2s 一发），控制端灰底提示、会话保持不断；Warn 同错误去重，
    // grab 恢复成功即重置
    uint32_t last_w = 0;  ///< 最近成功帧宽（0 → 占位帧兜底 640x480）
    uint32_t last_h = 0;
    int fail_ticks = 0;   ///< 连续 grab 失败 tick 数
    int warn_err = 0;     ///< 已告警错误码（去重；0=无）
    // 占位 tick 周期随自适应帧率走（逐 tick 重算，开销可忽略）
    auto placeholder_ticks_fn = [&interval] {
        return 2000 / std::max(1, static_cast<int>(interval.count()));
    };

    while (running_.load(std::memory_order_acquire)) {
        next_tick += interval;
        {
            foundation::ScopedPerf sp(frame_perf);   // grab+encode 全程计时
            if (capturer_->grab(frame)) {
                last_w = frame.width;
                last_h = frame.height;
                fail_ticks = 0;
                warn_err = 0;  // 恢复成功：告警去重重置
                // 会话水印（PRD P4）：grab 返回的 DIB 缓冲归本管线所有，encode
                // 前可就地写入；右下角 alpha 混合（1080p 贴片 ~60k 像素/帧）
                std::shared_ptr<std::vector<uint8_t>> wm;
                int wm_w = 0, wm_h = 0;
                {
                    std::lock_guard<std::mutex> lk(wm_mtx_);
                    wm = wm_;
                    wm_w = wm_w_;
                    wm_h = wm_h_;
                }
                if (wm != nullptr && wm_w > 0 && wm_h > 0
                    && frame.width >= static_cast<uint32_t>(wm_w)
                    && frame.height >= static_cast<uint32_t>(wm_h)) {
                    const int margin = 16;
                    const size_t stride =
                        static_cast<size_t>(frame.width) * 4;
                    uint8_t* base = const_cast<uint8_t*>(frame.data)
                                    + static_cast<size_t>(frame.height - wm_h - margin)
                                          * stride
                                    + static_cast<size_t>(frame.width - wm_w - margin) * 4;
                    const uint8_t* src = wm->data();
                    for (int row = 0; row < wm_h; ++row) {
                        uint8_t* dst = base + static_cast<size_t>(row) * stride;
                        const uint8_t* srow = src + static_cast<size_t>(row) * wm_w * 4;
                        for (int col = 0; col < wm_w; ++col) {
                            const uint8_t a = srow[col * 4 + 3];
                            if (a == 0) {
                                dst += 4;
                                continue;
                            }
                            // BGRA 直混（QImage ARGB32 小端布局同序）
                            if (a == 255) {
                                dst[0] = srow[col * 4];
                                dst[1] = srow[col * 4 + 1];
                                dst[2] = srow[col * 4 + 2];
                            }
                            else {
                                const uint32_t ia = 255 - a;
                                dst[0] = static_cast<uint8_t>(
                                    (srow[col * 4] * a + dst[0] * ia) / 255);
                                dst[1] = static_cast<uint8_t>(
                                    (srow[col * 4 + 1] * a + dst[1] * ia) / 255);
                                dst[2] = static_cast<uint8_t>(
                                    (srow[col * 4 + 2] * a + dst[2] * ia) / 255);
                            }
                            dst += 4;
                        }
                    }
                }
                std::vector<uint8_t> encoded;
                if (encoder_->encode(frame, encoded) && !encoded.empty()) {
                    protocol::ScreenFrameHeader hdr;
                    hdr.width = frame.width;
                    hdr.height = frame.height;
                    hdr.timestamp_us = frame.timestamp_us != 0 ? frame.timestamp_us : now_us();
                    auto pkt = protocol::build_screen_frame(hdr, encoded.data(), encoded.size());
                    const size_t dropped = queue_.push_overwrite(std::move(pkt));
                    dropped_total_.fetch_add(dropped, std::memory_order_relaxed);
                    // 自适应统计（Doc19 §4.5）：产出+被挤掉帧计入当前窗口
                    ++win_produced;
                    win_dropped += static_cast<int>(dropped);
                }
            }
            else {
                // 采集失败占位（Doc19 §4.2）：连续失败 ≥2s 且采集器给出错误
                // 分类才占位（未升级后端 last_error 恒 None → 维持旧行为）
                ++fail_ticks;
                const CaptureErr err = capturer_->last_error();
                if (err != CaptureErr::None && fail_ticks >= placeholder_ticks_fn()) {
                    const int err_code = static_cast<int>(err);
                    if (err_code != warn_err) {
                        warn_err = err_code;  // 同一错误只 Warn 一次，换错/恢复重置
                        SCLD_LOG(log_, LogLevel::Warn, "stream",
                                 "采集持续失败，改推占位帧 err="
                                     + std::to_string(err_code));
                    }
                    // 每 2s 一发（占位 tick 周期 = 失败门槛 tick 数，非每 tick）
                    if (fail_ticks % placeholder_ticks_fn() == 0) {
                        protocol::ScreenFrameHeader ph;
                        ph.width = last_w != 0 ? last_w : 640;
                        ph.height = last_h != 0 ? last_h : 480;
                        ph.timestamp_us = now_us();
                        ph.flags = 0x01
                                 | (err == CaptureErr::DesktopLocked ? 0x02 : 0x00);
                        auto pkt = protocol::build_screen_frame(ph, nullptr, 0);
                        dropped_total_.fetch_add(queue_.push_overwrite(std::move(pkt)),
                                                 std::memory_order_relaxed);
                    }
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
        // 自适应窗口统计：tick 计数 + 队列是否满（常满=发送链路消化不动）
        if (queue_.size() >= queue_.capacity()) {
            ++win_full_ticks;
        }
        ++win_ticks;
        // 自适应评估（5s 窗口，Doc19 §4.5）：两档闭环，档位变化只在此处发生
        const auto win_now = std::chrono::steady_clock::now();
        if (win_now - win_start >= std::chrono::seconds(5)) {
            const double drop_rate =
                win_produced > 0
                    ? static_cast<double>(win_dropped)
                          / static_cast<double>(win_produced)
                    : 0.0;
            const bool queue_stuffed =
                win_ticks > 0 && win_full_ticks * 5 >= win_ticks * 4;
            if (cur_tier == 0 && (drop_rate > kDropHigh || queue_stuffed)) {
                apply_tier(1);
            }
            else if (cur_tier == 1) {
                // 升档判据：连续 3 窗口（15s）低丢帧；任一窗口劣化即清零防振荡
                if (drop_rate < kDropLow && !queue_stuffed) {
                    if (++good_windows >= 3) {
                        good_windows = 0;
                        apply_tier(0);
                    }
                }
                else {
                    good_windows = 0;
                }
            }
            win_produced = 0;
            win_dropped = 0;
            win_ticks = 0;
            win_full_ticks = 0;
            win_start = win_now;
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
