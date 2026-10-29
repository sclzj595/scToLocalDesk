// ============================================================
// scToLocalDesk | src/service/blacklist_store.hpp
// 设备黑名单（Doc19 §3.4，P0）：与 TrustStore 白名单并行互斥
// （黑优先）。按 9 位连接码拉黑；被控端入站连接命中即静默丢弃
// ——不弹窗、不回 REJECT（不向拉黑方暴露任何存在性），仅审计。
// 持久层 blacklist.json 与 trust.json 同款：原子落盘（tmp+rename）、
// 连接码归一化去空格、mutex 线程安全。纯逻辑零 Qt
// ============================================================
#pragma once

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace sc::localdesk {

namespace foundation {
class ILogger;
}

class BlacklistStore final {
public:
    struct Entry {
        std::string code;        ///< 9 位裸数字连接码（主键）
        std::string note;        ///< 拉黑备注（为什么拉黑，可空）
        std::string blocked_at;  ///< 拉黑时间
    };

    /// exe 旁 blacklist.json；构造即加载（缺失/损坏视为空表）
    explicit BlacklistStore(std::filesystem::path file,
                            foundation::ILogger* logger = nullptr);

    /// 黑名单命中判定（入站过滤热路径：先查黑再查白，黑优先）
    bool is_blocked(const std::string& code) const;

    /// 加入黑名单（已存在则刷新 note/时间）；变更即原子落盘。
    /// 白名单互斥：同码在 TrustStore 的记录由调用方负责吊销
    /// （本类不反向依赖信任表，保持单向）
    void block(const std::string& code, const std::string& note);

    /// 移出黑名单（无此码为空操作，不触发落盘）；返回 false=无此码
    bool unblock(const std::string& code);

    void clear();

    size_t count() const;

    /// 全表快照（设置页黑名单管理列表数据源）
    std::vector<Entry> list() const;

private:
    void load();
    void save();  // 调用方须持锁

    std::filesystem::path file_;
    mutable std::mutex mtx_;
    std::vector<Entry> entries_;
    foundation::ILogger* log_{nullptr};
};

}  // namespace sc::localdesk
