// ============================================================
// scToLocalDesk | src/foundation/config_parser.hpp
// INI 配置存取：启动加载 → 严格校验（上层）→ 默认兜底 → 原子落盘
// （Doc12 §5 / Doc17 §5）线程安全。
// 原子写：tmp 文件 + rename 覆盖，防掉电截断
// ============================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "foundation/fs_portable.hpp"

namespace sc::localdesk::foundation {

class IniConfig final {
public:
    IniConfig() = default;
    explicit IniConfig(std::filesystem::path path)
        : path_(std::move(path)) {
    }

    /// 加载 ini；文件不存在返回 false（调用方用默认值初始化后 save）
    bool load() {
        std::lock_guard<std::mutex> lk(mtx_);
        data_.clear();
        std::error_code ec;
        if (!std::filesystem::exists(path_, ec)) {
            return false;
        }
        FILE* fp = fs_open(path_, "rb");  // 宽字符打开见 fs_portable.hpp
        if (fp == nullptr) {
            return false;
        }
        std::string section;
        char line[1024] = {};
        while (fgets(line, sizeof(line), fp) != nullptr) {
            std::string s = trim(line);
            if (s.empty() || s[0] == ';' || s[0] == '#') {
                continue;  // 注释
            }
            if (s.front() == '[' && s.back() == ']' && s.size() >= 3) {
                section = to_lower(trim(s.substr(1, s.size() - 2)));
                continue;
            }
            const auto eq = s.find('=');
            if (eq == std::string::npos) {
                continue;  // 畸形行忽略（Doc12 §5）
            }
            auto key = to_lower(trim(s.substr(0, eq)));
            auto val = trim(s.substr(eq + 1));
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
                val = val.substr(1, val.size() - 2);
            }
            if (!section.empty() && !key.empty()) {
                data_[section][key] = val;
            }
        }
        fclose(fp);
        dirty_ = true;  // 触发首次 save 补全默认值（Doc12 §5）
        return true;
    }

    /// 原子落盘：全量重写 tmp → rename 覆盖
    bool save() const {
        std::lock_guard<std::mutex> lk(mtx_);
        std::filesystem::path tmp = path_;
        tmp += L".tmp";

        FILE* fp = fs_open(tmp, "wb");
        if (fp == nullptr) {
            return false;
        }
        // 按 section 分组输出（保持插入序：用 vector 备份序）
        for (const auto& [sec, kv] : data_) {
            fputs(("[" + sec + "]\n").c_str(), fp);
            for (const auto& [k, v] : kv) {
                fputs((k + "=" + v + "\n").c_str(), fp);
            }
            fputc('\n', fp);
        }
        fclose(fp);

        std::error_code ec;
        // POSIX rename 不覆盖已存在目标，先清位（MSVC rename 自带覆盖，remove 无害）
        std::filesystem::remove(path_, ec);
        std::filesystem::rename(tmp, path_, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            return false;
        }
        dirty_ = false;
        return true;
    }

    // ---------------- 读取（缺失/非法返回默认值，Doc12 §5） ----------------

    std::string get_str(std::string_view sec, std::string_view key,
                                      std::string def) const {
        std::lock_guard<std::mutex> lk(mtx_);
        const auto s = data_.find(to_lower(sec));
        if (s == data_.end()) {
            return def;
        }
        const auto k = s->second.find(to_lower(key));
        return k == s->second.end() ? def : k->second;
    }

    int get_int(std::string_view sec, std::string_view key, int def) const {
        const auto s = get_str(sec, key, "");
        if (s.empty()) {
            return def;
        }
        char* end = nullptr;
        const long v = strtol(s.c_str(), &end, 10);
        return (end == s.c_str() || *end != '\0') ? def : static_cast<int>(v);
    }

    bool get_bool(std::string_view sec, std::string_view key, bool def) const {
        const auto s = to_lower(get_str(sec, key, ""));
        if (s == "true" || s == "1" || s == "yes" || s == "on") {
            return true;
        }
        if (s == "false" || s == "0" || s == "no" || s == "off") {
            return false;
        }
        return def;
    }

    // ---------------- 写入（仅内存，显式 save 落盘） ----------------

    void set_str(std::string_view sec, std::string_view key, std::string val) {
        std::lock_guard<std::mutex> lk(mtx_);
        data_[to_lower(sec)][to_lower(key)] = std::move(val);
        dirty_ = true;
    }

    void set_int(std::string_view sec, std::string_view key, int val) {
        set_str(sec, key, std::to_string(val));
    }

    void set_bool(std::string_view sec, std::string_view key, bool val) {
        set_str(sec, key, val ? "true" : "false");
    }

    bool dirty() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return dirty_;
    }

    void set_path(std::filesystem::path p) { path_ = std::move(p); }
    const std::filesystem::path& path() const noexcept { return path_; }

private:
    static std::string trim(const char* s) {
        return trim(std::string(s));
    }

    static std::string trim(std::string s) {
        // 含 \r\n（fgets 残留）与首尾空白
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '
                              || s.back() == '\t')) {
            s.pop_back();
        }
        size_t b = 0;
        while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) {
            ++b;
        }
        return s.substr(b);
    }

    static std::string to_lower(std::string s) {
        for (auto& c : s) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return s;
    }

    static std::string to_lower(std::string_view s) {
        return to_lower(std::string(s));
    }

    std::filesystem::path path_;
    // section -> (key -> value)，key 一律小写归一
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data_;
    mutable std::mutex mtx_;
    mutable bool dirty_{false};
};

}  // namespace sc::localdesk::foundation
