// ============================================================
// scToLocalDesk | src/service/trust_store.hpp
// 信任缓存落盘（Doc18 §3.2）：exe 旁 trust.json，确认弹窗勾选记忆的
// 持久层。字段 [{code, name, trusted_at}]；变更即原子落盘
// （tmp + rename，防断电半写）。中文设备名经 jsonx ensure_ascii=false
// 保持 UTF-8 明文。纯逻辑：文件 I/O 走宽字符 CRT（中文路径兼容）
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

class TrustStore final {
public:
    struct Entry {
        std::string code;
        std::string name;
        std::string note;        ///< 设备备注（PRD P6：标注对方是谁，可空）
        std::string trusted_at;
    };

    /// exe 旁 trust.json；构造即加载（缺失/损坏视为空表，下次 trust 重写）。
    /// logger 须随构造注入：加载日志发生在构造期
    explicit TrustStore(std::filesystem::path file,
                        foundation::ILogger* logger = nullptr);

    bool is_trusted(const std::string& code) const;

    /// 已存在则刷新 name/trusted_at（保留原 note），否则追加；变更即落盘
    void trust(const std::string& code, const std::string& name);

    /// 设置备注（PRD P6）：条目存在才改并落盘；返回 false=无此码
    bool set_note(const std::string& code, const std::string& note);

    /// 无此码为空操作（不触发落盘）
    void untrust(const std::string& code);

    void clear();

    size_t count() const;

    /// 全表快照（连接码页"最近连接"真数据源，Doc18 §3.2）
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
