// ============================================================
// scToLocalDesk | src/foundation/logger/spdlog_logger.hpp
// spdlog 实现：滚动文件（logs/）+ 控制台双 sink，异步模式（Doc12 §1）
// 仅当 SCTOLOCALDESK_WITH_SPDLOG=ON 时编入构建
// ============================================================
#pragma once

#include <string>

#include "foundation/logger/ilogger.hpp"

namespace sc::localdesk::foundation {

class SpdlogLogger final : public ILogger {
public:
    struct Options
    {
        std::string dir{"logs"};      ///< 日志目录（同 exe，Doc15 §1）
        std::string base_name{"sc_localdesk"};
        int max_size_mb{10};          ///< 单文件上限（Doc17 log.max_size_mb）
        int max_files{7};             ///< 保留份数（Doc17 log.max_files）
    };

    explicit SpdlogLogger(const Options& opt);
    ~SpdlogLogger() override;

    void log(LogLevel lv, std::string_view tag, std::string_view msg) override;
    void set_level(LogLevel lv) override;
    [[nodiscard]] LogLevel level() const override;

private:
    struct Impl;
    Impl* impl_{nullptr};
};

}  // namespace sc::localdesk::foundation
