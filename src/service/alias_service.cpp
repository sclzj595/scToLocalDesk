// ============================================================
// scToLocalDesk | src/service/alias_service.cpp
// 别名服务实现：在线表 + 冲突检测 + 事件发布（Doc07 §5/§6）
// ============================================================
#include "service/alias_service.hpp"

#include <chrono>

#include "core/events.hpp"
#include "foundation/event_bus.hpp"
#include "foundation/logger/ilogger.hpp"

namespace sc::localdesk {

namespace {

using sc::localdesk::foundation::LogLevel;

int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

Evt_DeviceOnline make_online_evt(const DeviceInfo& dev) {
    Evt_DeviceOnline e;
    e.device_alias = dev.alias;
    e.ip = dev.ip;
    e.port = dev.port;
    e.timestamp_us = now_us();
    return e;
}

}  // namespace

AliasService::AliasService(std::shared_ptr<foundation::EventBus> bus,
                           std::shared_ptr<foundation::ILogger> logger)
    : bus_(std::move(bus)),
      log_(std::move(logger)) {
}

void AliasService::upsert_device(const DeviceInfo& dev) {
    if (dev.alias.empty() || dev.ip.empty() || dev.port == 0) {
        SCLD_LOG(log_, LogLevel::Warn, "discovery", "心跳字段非法，丢弃");
        return;
    }
    const int64_t now = now_us();
    std::lock_guard<std::mutex> lk(mtx_);
    const auto it = table_.find(dev.alias);
    if (it == table_.end()) {
        DeviceInfo info = dev;
        info.last_seen_us = now;
        table_.emplace(dev.alias, info);
        SCLD_LOG(log_, LogLevel::Info, "discovery",
                 "[alias=" + dev.alias + "] 设备上线 ip=" + dev.ip);
        if (bus_) {
            bus_->publish(make_online_evt(info));
        }
        return;
    }
    // 别名冲突：同别名异 IP 且旧条目未超时（Doc07 §6.2）
    if (it->second.ip != dev.ip && now - it->second.last_seen_us < offline_timeout_us_) {
        if (bus_) {
            Evt_AliasConflict e;
            e.device_alias = dev.alias;
            e.conflict_ip = dev.ip;
            e.timestamp_us = now;
            bus_->publish(e);
        }
        SCLD_LOG(log_, LogLevel::Warn,
                 "discovery", "[alias=" + dev.alias + "] 别名冲突 old_ip="
                     + it->second.ip + " new_ip=" + dev.ip);
    }
    it->second.ip = dev.ip;
    it->second.port = dev.port;
    it->second.status = dev.status;
    it->second.version = dev.version;
    it->second.last_seen_us = now;
}

std::optional<DeviceInfo> AliasService::resolve(const std::string& alias) const {
    std::lock_guard<std::mutex> lk(mtx_);
    const auto it = table_.find(alias);
    if (it == table_.end()) {
        return std::nullopt;
    }
    if (now_us() - it->second.last_seen_us > offline_timeout_us_) {
        return std::nullopt;  // 已超时视为离线
    }
    return it->second;
}

std::vector<DeviceInfo> AliasService::online_devices() const {
    const int64_t now = now_us();
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<DeviceInfo> out;
    out.reserve(table_.size());
    for (const auto& [alias, info] : table_) {
        if (now - info.last_seen_us <= offline_timeout_us_) {
            out.push_back(info);
        }
    }
    return out;
}

void AliasService::cleanup_expired() {
    const int64_t now = now_us();
    std::vector<std::string> expired;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto it = table_.begin(); it != table_.end();) {
            if (now - it->second.last_seen_us > offline_timeout_us_) {
                expired.push_back(it->first);
                it = table_.erase(it);
            }
            else {
                ++it;
            }
        }
    }
    if (bus_ != nullptr) {
        for (const auto& alias : expired) {
            Evt_DeviceOffline e;
            e.device_alias = alias;
            e.timestamp_us = now;
            bus_->publish(e);
            SCLD_LOG(log_, LogLevel::Info, "discovery", "[alias=" + alias + "] 设备离线");
        }
    }
}

void AliasService::set_self_alias(std::string alias) {
    std::lock_guard<std::mutex> lk(mtx_);
    self_alias_ = std::move(alias);
}

const std::string& AliasService::self_alias() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return self_alias_;
}

void AliasService::set_offline_timeout(int64_t timeout_us) {
    std::lock_guard<std::mutex> lk(mtx_);
    offline_timeout_us_ = timeout_us;
}

void AliasService::set_static_map(const std::vector<DeviceInfo>& table) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& dev : table) {
        DeviceInfo info = dev;
        info.last_seen_us = now_us();  // 静态条目视为常在（Doc07 §7）
        table_.insert_or_assign(dev.alias, std::move(info));
    }
}

}  // namespace sc::localdesk
