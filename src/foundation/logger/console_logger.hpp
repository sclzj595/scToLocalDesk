// ============================================================
// scToLocalDesk | src/foundation/logger/console_logger.hpp
// 内置兜底日志实现：控制台输出（core-only 冒烟 / 单元测试 Mock 用）
// 无任何第三方依赖；线程安全
// ============================================================
#pragma once

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>

#include "foundation/logger/ilogger.hpp"

namespace sc::localdesk::foundation {

class ConsoleLogger final : public ILogger {
public:
    void log(LogLevel lv, std::string_view tag, std::string_view msg) override {
        if (lv < level_.load(std::memory_order_relaxed) || lv >= LogLevel::Off) {
            return;
        }
        static constexpr const char* kNames[] = {
            "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRIT"};
        std::lock_guard<std::mutex> lk(mtx_);
        std::fprintf(stderr, "[%s] [%.*s] %.*s\n",
                     kNames[static_cast<int>(lv)],
                     static_cast<int>(tag.size()), tag.data(),
                     static_cast<int>(msg.size()), msg.data());
    }

    void set_level(LogLevel lv) override { level_.store(lv, std::memory_order_relaxed); }
    [[nodiscard]] LogLevel level() const override {
        return level_.load(std::memory_order_relaxed);
    }

private:
    std::mutex mtx_;
    std::atomic<LogLevel> level_{LogLevel::Info};
};

}  // namespace sc::localdesk::foundation
