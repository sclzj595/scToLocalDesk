// ============================================================
// scToLocalDesk | src/foundation/logger/ilogger.hpp
// 日志抽象契约（Doc06 §6 / Doc12 §1）；实现：ConsoleLogger（内置兜底）、
// FileLogger（滚动文件兜底）、SpdlogLogger（SCLD_HAS_SPDLOG：vendored
// third_party/spdlog 优先，CMake 自动探测，find_package 兜底）
// ============================================================
#pragma once

#include <string_view>

namespace sc::localdesk::foundation {

enum class LogLevel : int {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
    Off,
};

class ILogger {
public:
    virtual ~ILogger() = default;

    /// tag 为模块名（如 "session"/"auth"），msg 为正文；
    /// 结构化字段由调用方拼接进 msg（[session_id=..][alias=..]，Doc12 §1）
    virtual void log(LogLevel lv, std::string_view tag, std::string_view msg) = 0;

    virtual void set_level(LogLevel lv) = 0;

    [[nodiscard]] virtual LogLevel level() const = 0;
};

/// 统一入口宏：日志调用点统一走此路径，便于全局降噪
#define SCLD_LOG(logger, lv, tag, msg)                    \
    do {                                                  \
        if (logger) {                                     \
            (logger)->log(lv, tag, msg);                  \
        }                                                 \
    } while (0)

}  // namespace sc::localdesk::foundation
