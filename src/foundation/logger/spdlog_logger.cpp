// ============================================================
// scToLocalDesk | src/foundation/logger/spdlog_logger.cpp
// pimpl 隔离 spdlog 头依赖：上层永远只见 ilogger.hpp
// ============================================================
#include "foundation/logger/spdlog_logger.hpp"

#include <spdlog/async.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>

namespace sc::localdesk::foundation {

namespace {

spdlog::level::level_enum to_spd(LogLevel lv) {
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

LogLevel from_spd(spdlog::level::level_enum lv) {
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

}  // namespace

struct SpdlogLogger::Impl {
    std::shared_ptr<spdlog::logger> logger;
};

SpdlogLogger::SpdlogLogger(const Options& opt) {
    // 异步线程池：队列满丢弃 debug 级，不阻塞业务线程（Doc12 §1 红线）
    spdlog::init_thread_pool(8192, 1);

    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto rotating = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        opt.dir + "/" + opt.base_name + ".log",
        static_cast<size_t>(opt.max_size_mb) * 1024 * 1024, opt.max_files);

    impl_ = new Impl();
    impl_->logger = std::make_shared<spdlog::async_logger>(
        "sc_localdesk",
        spdlog::sinks_init_list{console, rotating},
        spdlog::thread_pool(),
        spdlog::async_overflow_policy::overrun_oldest);
    impl_->logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
    impl_->logger->set_level(spdlog::level::info);
    spdlog::register_logger(impl_->logger);
}

SpdlogLogger::~SpdlogLogger() {
    if (impl_ != nullptr) {
        impl_->logger->flush();
        spdlog::drop("sc_localdesk");
        delete impl_;
        impl_ = nullptr;
    }
}

void SpdlogLogger::log(LogLevel lv, const std::string& tag, const std::string& msg) {
    if (impl_ == nullptr) {
        return;
    }
    impl_->logger->log(to_spd(lv), "[{}] {}", tag, msg);
}

void SpdlogLogger::set_level(LogLevel lv) {
    if (impl_ != nullptr) {
        impl_->logger->set_level(to_spd(lv));
    }
}

LogLevel SpdlogLogger::level() const {
    return impl_ != nullptr ? from_spd(impl_->logger->level()) : LogLevel::Off;
}

}  // namespace sc::localdesk::foundation
