// ============================================================
// scToLocalDesk | src/foundation/perf_counter.hpp
// 微秒级性能计数器（吸收 YD/NetComm CMicroCounter 思想，现代化重写）：
// - QueryPerformanceCounter → std::chrono::steady_clock（MSVC 底层同为
//   QPC，跨平台；Linux Qt 6.2 远程编译同样可用）
// - 保留原版精髓：累计耗时 + 调用次数 → 均值；新增峰值统计
// - 新增 ScopedPerf RAII：作用域退出自动计数，配合阈值告警接线
// 用途：采集/编码/发送等热路径的耗时可见性（性能回归的 first line）
// 线程安全：单实例仅限属主线程使用（推流采集循环等单线程场景）
// ============================================================
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace sc::localdesk::foundation {

class PerfCounter final {
public:
    /// 开始计时
    void start() {
        begin_ = clock::now();
    }

    /// 结束计时并累计；返回本次耗时（毫秒）
    double stop() {
        const auto dt = std::chrono::duration<double, std::milli>(clock::now() - begin_);
        const double ms = dt.count();
        ++count_;
        total_ms_ += ms;
        max_ms_ = std::max(max_ms_, ms);
        last_ms_ = ms;
        return ms;
    }

    void reset() {
        count_ = 0;
        total_ms_ = 0.0;
        max_ms_ = 0.0;
        last_ms_ = 0.0;
    }

    [[nodiscard]] uint64_t count() const { return count_; }
    [[nodiscard]] double last_ms() const { return last_ms_; }
    [[nodiscard]] double max_ms() const { return max_ms_; }
    /// 平均耗时（毫秒）；无样本返回 0
    [[nodiscard]] double avg_ms() const {
        return count_ > 0 ? total_ms_ / static_cast<double>(count_) : 0.0;
    }
    [[nodiscard]] double total_ms() const { return total_ms_; }

private:
    using clock = std::chrono::steady_clock;
    clock::time_point begin_{};
    uint64_t count_{0};
    double total_ms_{0.0};
    double max_ms_{0.0};
    double last_ms_{0.0};
};

/// RAII 作用域计时：构造即 start，析构即 stop 并写入 counter
/// （stop 后可通过 counter.avg_ms()/max_ms() 做周期性统计上报）
class ScopedPerf final {
public:
    explicit ScopedPerf(PerfCounter& counter) : counter_(counter) {
        counter_.start();
    }
    ~ScopedPerf() {
        counter_.stop();
    }
    ScopedPerf(const ScopedPerf&) = delete;
    ScopedPerf& operator=(const ScopedPerf&) = delete;

private:
    PerfCounter& counter_;
};

}  // namespace sc::localdesk::foundation
