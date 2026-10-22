// ============================================================
// scToLocalDesk | src/service/screen_stream_service.hpp
// 被控端推流管线（Doc10 §2）：
//   采集线程 grab → encode → build_screen_frame → 队列(push_overwrite 只留最新)
//   发送线程 pop → SendFn（会话 send_business_frame）
// stop() 仅置位+关队列（非阻塞，可持会话锁调用）；析构 join
// ============================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

#include "foundation/bounded_queue.hpp"

namespace sc::localdesk {

class IScreenCapturer;
class IEncoder;

namespace foundation {
class EventBus;
class ILogger;
}

class ScreenStreamService final {
public:
    /// 发送出口：通常绑定 RemoteSessionService::send_business_frame
    using SendFn = std::function<bool(const std::vector<uint8_t>&)>;

    struct Options {
        int fps{15};              // Doc17 session.fps：10/15/24/30
        int quality{80};          // Doc17 session.jpeg_quality（custom 时）
    };

    ScreenStreamService(std::shared_ptr<foundation::EventBus> bus,
                        std::shared_ptr<foundation::ILogger> logger);
    ~ScreenStreamService();

    /// 注入采集/编码实现（main 组装；采集失败自动 warn 降级由实现层负责）
    void set_source(std::shared_ptr<IScreenCapturer> capturer,
                    std::shared_ptr<IEncoder> encoder);

    /// 启动推流（会话 Running 时调用）；已在跑则先停
    bool start(SendFn send, const Options& opt);
    /// 停止（非阻塞；线程自行退出）
    void stop();

    [[nodiscard]] bool running() const { return running_.load(std::memory_order_acquire); }
    [[nodiscard]] uint64_t total_dropped() const {
        return dropped_total_.load(std::memory_order_relaxed);
    }

private:
    void capture_loop(SendFn send, Options opt);
    void send_loop(SendFn send);

    std::shared_ptr<foundation::EventBus> bus_;
    std::shared_ptr<foundation::ILogger> log_;
    std::shared_ptr<IScreenCapturer> capturer_;
    std::shared_ptr<IEncoder> encoder_;

    foundation::BoundedQueue<std::vector<uint8_t>> queue_{4};  // 只留最新帧（Doc10 §3）
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> dropped_total_{0};
    std::thread capture_thread_;
    std::thread send_thread_;
};

}  // namespace sc::localdesk
