// ============================================================
// scToLocalDesk | src/service/trust_store.cpp
// 加载/落盘实现：宽字符 CRT + jsonx，rename 原子覆盖
// ============================================================
#include "service/trust_store.hpp"

#include <cstdio>
#include <ctime>

#include "foundation/fs_portable.hpp"
#include "foundation/jsonx.hpp"
#include "foundation/logger/ilogger.hpp"

namespace sc::localdesk {

namespace {

using foundation::json;
using foundation::json_dump;
using foundation::json_parse;
using foundation::LogLevel;

std::string now_timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm {};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

std::string read_all(const std::filesystem::path& p) {
    FILE* fp = foundation::fs_open(p, "rb");
    if (fp == nullptr) {
        return {};
    }
    std::string out;
    char buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        out.append(buf, n);
    }
    fclose(fp);
    return out;
}

}  // namespace

TrustStore::TrustStore(std::filesystem::path file, foundation::ILogger* logger)
    : file_(std::move(file)),
      log_(logger) {
    load();
}

void TrustStore::load() {
    std::lock_guard<std::mutex> lk(mtx_);
    entries_.clear();
    const std::string text = read_all(file_);
    if (text.empty()) {
        return;  // 首次运行无文件
    }
    const auto root = json_parse(text);
    if (!root.has_value() || !root->is_array()) {
        SCLD_LOG(log_, LogLevel::Warn, "trust",
                 "trust.json 格式非法，按空表处理（下次变更重写）");
        return;
    }
    for (const auto& item : *root) {
        if (!item.is_object() || !item.contains("code")
            || !item.at("code").is_string()) {
            continue;  // 畸形条目跳过
        }
        Entry e;
        e.code = item.at("code").get<std::string>();
        if (item.contains("name") && item.at("name").is_string()) {
            e.name = item.at("name").get<std::string>();
        }
        if (item.contains("trusted_at") && item.at("trusted_at").is_string()) {
            e.trusted_at = item.at("trusted_at").get<std::string>();
        }
        entries_.push_back(std::move(e));
    }
    SCLD_LOG(log_, LogLevel::Info, "trust",
             "trust.json 已加载 count=" + std::to_string(entries_.size()));
}

void TrustStore::save() {
    json arr = json::array();
    for (const auto& e : entries_) {
        arr.push_back(json{{"code", e.code},
                           {"name", e.name},
                           {"trusted_at", e.trusted_at}});
    }
    std::filesystem::path tmp = file_;
    tmp += L".tmp";
    FILE* fp = foundation::fs_open(tmp, "wb");
    if (fp == nullptr) {
        SCLD_LOG(log_, LogLevel::Error, "trust", "trust.json 临时文件写入失败");
        return;
    }
    const std::string dump = json_dump(arr);
    const bool ok = fwrite(dump.data(), 1, dump.size(), fp) == dump.size();
    fclose(fp);
    if (!ok) {
        SCLD_LOG(log_, LogLevel::Error, "trust", "trust.json 写入不完整");
        return;
    }
    std::error_code ec;
    // POSIX rename 不覆盖已存在目标，先清位（Windows rename 自带覆盖，remove 无害）
    std::filesystem::remove(file_, ec);
    std::filesystem::rename(tmp, file_, ec);
    if (ec) {
        SCLD_LOG(log_, LogLevel::Error, "trust",
                 "trust.json rename 失败 ec=" + std::to_string(ec.value()));
    }
}

bool TrustStore::is_trusted(const std::string& code) const {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& e : entries_) {
        if (e.code == code) {
            return true;
        }
    }
    return false;
}

void TrustStore::trust(const std::string& code, const std::string& name) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& e : entries_) {
        if (e.code == code) {
            e.name = name;
            e.trusted_at = now_timestamp();
            save();
            return;
        }
    }
    entries_.push_back(Entry{code, name, now_timestamp()});
    save();
}

void TrustStore::untrust(const std::string& code) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->code == code) {
            entries_.erase(it);
            save();
            return;
        }
    }
}

void TrustStore::clear() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (entries_.empty()) {
        return;
    }
    entries_.clear();
    save();
}

size_t TrustStore::count() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return entries_.size();
}

}  // namespace sc::localdesk
