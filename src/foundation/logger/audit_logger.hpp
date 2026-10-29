// ============================================================
// scToLocalDesk | src/foundation/logger/audit_logger.hpp
// 安全审计日志独立分流（Doc19 §6，P0）：与运行调试日志分离的
// 独立文件 audit.log，企业内网追溯刚需——连接/同意/拒绝/黑名单
// 命中/信任变更/会话起止全部落此文件，不允许普通用户清空。
// - 固定行格式：时间 | 本机码 | 对端码 | 事件 | 结果 | IP | 备注
//   （Doc19 §6 规定七列，管道分隔便于 IT 侧 grep/导入表格）
// - 与主日志同滚动参数（5MB×5）：FileLogger 复用，零新依赖
// - 无级别概念：审计事件全量记录，不受 set_level 降噪影响
// - 线程安全（FileLogger 内部 mutex）；核心层零 Qt
// 用法：装配层创建一次（logs/audit.log + 本机连接码），注入
// RemoteSessionService / BlacklistStore 等审计点写事件
// ============================================================
#pragma once

#include <filesystem>
#include <string>

#include "foundation/logger/file_logger.hpp"

namespace sc::localdesk::foundation {

class AuditLogger final {
public:
    /// file：audit.log 全路径；self_code：本机 9 位连接码（行内第二列）
    /// 去空格归一化由调用方保证（IdService self_code() 已是裸数字）
    explicit AuditLogger(std::filesystem::path file, std::string self_code)
        : file_(std::move(file)),
          self_code_(std::move(self_code)),
          backend_(file_, 5 * 1024 * 1024, 5, false) {}

    /// 审计事件唯一入口（Doc19 §6 七列格式）
    /// event：Request/Approve/Reject/AutoApprove/SessionStart/SessionEnd/
    ///        BlacklistHit/BlacklistAdd/BlacklistRemove/BlacklistClear/
    ///        TrustAdd/TrustRemove/UnattendedAuth...
    /// result：OK/Denied/Timeout/Busy/Error...
    /// peer_code：对端 9 位连接码（未知传 "-"）；peer_ip：对端 IP（未知 "-"）
    void audit(const std::string& event, const std::string& result,
               const std::string& peer_code, const std::string& peer_ip,
               const std::string& note) {
        // 行格式：ts | self | peer | event | result | ip | note
        std::string line = timestamp_now();
        line += " | ";
        line += self_code_;
        line += " | ";
        line += peer_code.empty() ? "-" : peer_code;
        line += " | ";
        line += event;
        line += " | ";
        line += result;
        line += " | ";
        line += peer_ip.empty() ? "-" : peer_ip;
        line += " | ";
        line += note;
        // tag 固定 "audit"，级别 Warn：与主日志既有审计口径对齐，
        // 主日志保留一份 Warn 摘要（现场排障无需换文件），audit.log 存全量
        backend_.log(LogLevel::Warn, "audit", line);
    }

private:
    /// 本地时间戳（秒精度；七列首列）
    static std::string timestamp_now() {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[32] = {};
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        return buf;
    }

    std::filesystem::path file_;  ///< 预留：后续按需扩展专用写路径（现走 backend_）
    std::string self_code_;
    FileLogger backend_;  ///< 复用滚动文件日志（audit.log 5MB×5，每行 flush）
};

}  // namespace sc::localdesk::foundation
