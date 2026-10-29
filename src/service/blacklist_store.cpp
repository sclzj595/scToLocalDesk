// ============================================================
// scToLocalDesk | src/service/blacklist_store.cpp
// 加载/落盘实现：与 trust_store.cpp 同款（宽字符 CRT + jsonx +
// rename 原子覆盖），行为口径一致便于审计与排障
// ============================================================
#include "service/blacklist_store.hpp"

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

/// 连接码归一化：去空格（与 TrustStore::normalize_code 同口径，
/// UI 两种形态 / 服务层数字码三种来源必须同形才能命中）
std::string normalize_code(const std::string& code) {
    std::string out;
    out.reserve(code.size());
    for (const char c : code) {
        if (c != ' ') {
            out.push_back(c);
        }
    }
    return out;
}

}  // namespace

BlacklistStore::BlacklistStore(std::filesystem::path file,
                               foundation::ILogger* logger)
    : file_(std::move(file)),
      log_(logger) {
    load();
}

void BlacklistStore::load() {
    std::lock_guard<std::mutex> lk(mtx_);
    entries_.clear();
    const std::string text = read_all(file_);
    if (text.empty()) {
        return;  // 首次运行无文件
    }
    json root;
    if (!json_parse(text, root) || !root.is_array()) {
        SCLD_LOG(log_, LogLevel::Warn, "blacklist",
                 "blacklist.json 格式非法，按空表处理（下次变更重写）");
        return;
    }
    for (const auto& item : root) {
        if (!item.is_object() || !item.contains("code")
            || !item.at("code").is_string()) {
            continue;  // 畸形条目跳过
        }
        Entry e;
        e.code = normalize_code(item.at("code").get<std::string>());
        if (e.code.empty()) {
            continue;
        }
        if (item.contains("note") && item.at("note").is_string()) {
            e.note = item.at("note").get<std::string>();
        }
        if (item.contains("blocked_at") && item.at("blocked_at").is_string()) {
            e.blocked_at = item.at("blocked_at").get<std::string>();
        }
        entries_.push_back(std::move(e));
    }
    SCLD_LOG(log_, LogLevel::Info, "blacklist",
             "blacklist.json 已加载 count=" + std::to_string(entries_.size()));
}

void BlacklistStore::save() {
    json arr = json::array();
    for (const auto& e : entries_) {
        arr.push_back(json{{"code", e.code},
                           {"note", e.note},
                           {"blocked_at", e.blocked_at}});
    }
    std::filesystem::path tmp = file_;
    tmp += L".tmp";
    FILE* fp = foundation::fs_open(tmp, "wb");
    if (fp == nullptr) {
        SCLD_LOG(log_, LogLevel::Error, "blacklist",
                 "blacklist.json 临时文件写入失败");
        return;
    }
    const std::string dump = json_dump(arr);
    const bool ok = fwrite(dump.data(), 1, dump.size(), fp) == dump.size();
    fclose(fp);
    if (!ok) {
        SCLD_LOG(log_, LogLevel::Error, "blacklist", "blacklist.json 写入不完整");
        return;
    }
    std::error_code ec;
    // POSIX rename 不覆盖已存在目标，先清位（Windows rename 自带覆盖，remove 无害）
    std::filesystem::remove(file_, ec);
    std::filesystem::rename(tmp, file_, ec);
    if (ec) {
        SCLD_LOG(log_, LogLevel::Error, "blacklist",
                 "blacklist.json rename 失败 ec=" + std::to_string(ec.value()));
    }
}

bool BlacklistStore::is_blocked(const std::string& code) const {
    const std::string norm = normalize_code(code);
    if (norm.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& e : entries_) {
        if (e.code == norm) {
            return true;
        }
    }
    return false;
}

void BlacklistStore::block(const std::string& code, const std::string& note) {
    const std::string norm = normalize_code(code);
    if (norm.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& e : entries_) {
        if (e.code == norm) {
            e.note = note;
            e.blocked_at = now_timestamp();
            save();
            return;
        }
    }
    entries_.push_back(Entry{norm, note, now_timestamp()});
    save();
}

bool BlacklistStore::unblock(const std::string& code) {
    const std::string norm = normalize_code(code);
    if (norm.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->code == norm) {
            entries_.erase(it);
            save();
            return true;
        }
    }
    return false;
}

void BlacklistStore::clear() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (entries_.empty()) {
        return;
    }
    entries_.clear();
    save();
}

size_t BlacklistStore::count() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return entries_.size();
}

std::vector<BlacklistStore::Entry> BlacklistStore::list() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return entries_;
}

}  // namespace sc::localdesk
