// ============================================================
// scToLocalDesk | src/foundation/logger/file_logger.hpp
// 文件日志 + 滚动归档（Doc12 §1）：exe 旁 logs/ 落盘，客户现场排障刚需
// - 5MB×5 滚动：超限将 .log → .log.1 → ... → .log.N（N 溢出删除）
// - 每行落盘即 flush：进程崩溃/断电前最后一行可追溯
// - 宽字符路径安全：std::filesystem 全程窄转换零依赖（中文安装路径可写）
// - 打开失败（目录只读/被占用）自动退化为 stderr，绝不因日志阻断业务
// - 可选 echo_stderr：开发期同步回显控制台
// 线程安全（mutex）；header-only，随用随包含
// ============================================================
#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

#include "foundation/logger/ilogger.hpp"

namespace sc::localdesk::foundation {

class FileLogger final : public ILogger {
public:
    /// file：日志文件全路径（含文件名）；目录不存在自动创建
    explicit FileLogger(std::filesystem::path file, size_t max_bytes = 5 * 1024 * 1024,
                        int max_files = 5, bool echo_stderr = false)
        : file_(std::move(file)), max_bytes_(max_bytes > 0 ? max_bytes : 1),
          max_files_(max_files > 0 ? max_files : 1), echo_stderr_(echo_stderr) {
        open_locked();
    }

    ~FileLogger() override {
        std::lock_guard<std::mutex> lk(mtx_);
        if (out_.is_open()) {
            out_.flush();
            out_.close();
        }
    }

    FileLogger(const FileLogger&) = delete;
    FileLogger& operator=(const FileLogger&) = delete;

    void log(LogLevel lv, std::string_view tag, std::string_view msg) override {
        if (lv < level_.load(std::memory_order_relaxed) || lv >= LogLevel::Off) {
            return;
        }
        static constexpr const char* kNames[] = {
            "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRIT"};

        // 时间戳：本地时间 + 毫秒（客户回传日志时对得上现场时间）
        const auto now = std::chrono::system_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch())
                            .count() % 1000;
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char ts[32];
        std::snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                      tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                      tmv.tm_hour, tmv.tm_min, tmv.tm_sec, static_cast<int>(ms));

        std::string line;
        line.reserve(32 + tag.size() + msg.size());
        line += ts;
        line += " [";
        line += kNames[static_cast<int>(lv)];
        line += "] [";
        line.append(tag.data(), tag.size());
        line += "] ";
        line.append(msg.data(), msg.size());
        line += '\n';

        std::lock_guard<std::mutex> lk(mtx_);
        if (echo_stderr_) {
            std::fwrite(line.data(), 1, line.size(), stderr);
            std::fflush(stderr);
        }
        if (broken_) {
            return;  // 文件不可用：仅 stderr 通路（构造期已退化）
        }
        if (written_ + line.size() > max_bytes_) {
            rotate_locked();
        }
        if (out_.is_open()) {
            out_.write(line.data(), static_cast<std::streamsize>(line.size()));
            out_.flush();   // 每行落盘：崩溃前最后一行可追溯
            written_ += line.size();
            if (!out_.good()) {
                // 磁盘满/句柄失效：退化 stderr，本轮起不再写文件
                out_.close();
                broken_ = true;
                std::fputs("[file_logger] 文件写入失败，退化为 stderr\n", stderr);
            }
        }
    }

    void set_level(LogLevel lv) override { level_.store(lv, std::memory_order_relaxed); }
    [[nodiscard]] LogLevel level() const override {
        return level_.load(std::memory_order_relaxed);
    }

private:
    /// 打开文件流（构造期调用一次）；失败置 broken（后续仅 stderr）
    void open_locked() {
        std::error_code ec;
        const auto dir = file_.parent_path();
        if (!dir.empty()) {
            std::filesystem::create_directories(dir, ec);
        }
        // 追加模式：跨次启动续写，滚动按累计大小判断
        out_.open(file_, std::ios::app | std::ios::binary);
        if (!out_.is_open()) {
            broken_ = true;
            std::fputs("[file_logger] 日志文件打开失败，退化为 stderr\n", stderr);
            return;
        }
        std::error_code sz_ec;
        const auto sz = std::filesystem::file_size(file_, sz_ec);
        written_ = sz_ec ? 0 : static_cast<size_t>(sz);
    }

    /// 滚动归档：.log.N-1 → .log.N 链式后移，末位删除，当前文件让位 .log.1
    void rotate_locked() {
        out_.close();
        for (int i = max_files_ - 1; i >= 1; --i) {
            const auto from = archive_path(i);
            const auto to = archive_path(i + 1);
            std::error_code ec;
            std::filesystem::remove(to, ec);   // POSIX rename 不覆盖，先清位
            std::filesystem::rename(from, to, ec);
        }
        std::error_code ec;
        std::filesystem::remove(archive_path(1), ec);
        std::filesystem::rename(file_, archive_path(1), ec);
        if (ec) {
            // 让位失败（文件被杀软/编辑器占用）：直接截断续写，不丢日志通路
            std::fputs("[file_logger] 滚动失败，截断续写\n", stderr);
        }
        out_.open(file_, std::ios::trunc | std::ios::binary);
        written_ = 0;
        if (!out_.is_open()) {
            broken_ = true;
        }
    }

    /// 第 n 代归档路径：scToLocalDesk.log → scToLocalDesk.log.1（ASCII 后缀拼接跨平台安全）
    [[nodiscard]] std::filesystem::path archive_path(int n) const {
        std::filesystem::path p = file_;
        p += "." + std::to_string(n);
        return p;
    }

    std::filesystem::path file_;
    size_t max_bytes_;
    int max_files_;
    bool echo_stderr_;
    std::ofstream out_;
    size_t written_{0};      ///< 当前文件累计写入字节（滚动判据）
    bool broken_{false};     ///< 文件通路不可用（仅 stderr）
    std::mutex mtx_;
    std::atomic<LogLevel> level_{LogLevel::Info};
};

}  // namespace sc::localdesk::foundation
