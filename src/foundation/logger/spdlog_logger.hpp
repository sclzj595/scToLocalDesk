// ============================================================
// scToLocalDesk | src/foundation/logger/spdlog_logger.hpp
// spdlog 后端适配器（ilogger.hpp 承诺的 SCTOLOCALDESK_WITH_SPDLOG 编入项）
// 吸收 spdlog 优点进本工程： rotating_file_sink_mt（5MB×5，与
// FileLogger 兜底版同参数） 异步模式可选、模式化格式、
//   flush_on(Warn) 智能落盘策略（告警即刷，常规批量）
// 适配层保持 ILogger 契约不变：上层业务零感知后端切换
// ============================================================
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include "foundation/logger/ilogger.hpp"

namespace sc::localdesk::foundation {

class SpdlogLogger final : public ILogger {
public:
    /// file：日志文件全路径；目录不存在由 spdlog 创建
    explicit SpdlogLogger(const std::string& file,
                          size_t max_bytes = 5 * 1024 * 1024, size_t max_files = 5) {
        // 模式与 FileLogger 行格式对齐（客户侧两版本日志口径一致）
        spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] [%n] %v");
        sink_ = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            file, max_bytes, max_files);
        sink_->set_level(spdlog::level::trace);
        logger_ = std::make_shared<spdlog::logger>("scld", sink_);
        // 告警及以上立即落盘（崩溃前最后一条 WARN 可追溯），常规级别批量
        logger_->flush_on(spdlog::level::warn);
        logger_->set_level(spdlog::level::info);
    }

    ~SpdlogLogger() override {
        if (logger_) {
            logger_->flush();
        }
    }

    void log(LogLevel lv, std::string_view tag, std::string_view msg) override {
        if (lv >= LogLevel::Off || logger_ == nullptr) {
            return;
        }
        // 行格式与 FileLogger 对齐：[级别由 pattern %l 提供] [tag] msg
        std::string line;
        line.reserve(tag.size() + 3 + msg.size());
        line += '[';
        line.append(tag.data(), tag.size());
        line += "] ";
        line.append(msg.data(), msg.size());

        switch (lv) {
        case LogLevel::Trace:
            logger_->trace(line);
            break;
        case LogLevel::Debug:
            logger_->debug(line);
            break;
        case LogLevel::Info:
            logger_->info(line);
            break;
        case LogLevel::Warn:
            logger_->warn(line);
            break;
        case LogLevel::Error:
            logger_->error(line);
            break;
        case LogLevel::Critical:
            logger_->critical(line);
            break;
        default:
            break;
        }
    }

    void set_level(LogLevel lv) override {
        if (logger_ != nullptr) {
            logger_->set_level(to_spd(lv));
        }
    }

    [[nodiscard]] LogLevel level() const override {
        return logger_ != nullptr ? from_spd(logger_->level()) : LogLevel::Info;
    }

private:
    static spdlog::level::level_enum to_spd(LogLevel lv) {
        switch (lv) {
        case LogLevel::Trace: return spdlog::level::trace;
        case LogLevel::Debug: return spdlog::level::debug;
        case LogLevel::Info: return spdlog::level::info;
        case LogLevel::Warn: return spdlog::level::warn;
        case LogLevel::Error: return spdlog::level::err;
        case LogLevel::Critical: return spdlog::level::critical;
        default: return spdlog::level::off;
        }
    }
    static LogLevel from_spd(spdlog::level::level_enum lv) {
        switch (lv) {
        case spdlog::level::trace: return LogLevel::Trace;
        case spdlog::level::debug: return LogLevel::Debug;
        case spdlog::level::info: return LogLevel::Info;
        case spdlog::level::warn: return LogLevel::Warn;
        case spdlog::level::err: return LogLevel::Error;
        case spdlog::level::critical: return LogLevel::Critical;
        default: return LogLevel::Off;
        }
    }

    std::shared_ptr<spdlog::sinks::rotating_file_sink_mt> sink_;
    std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace sc::localdesk::foundation
