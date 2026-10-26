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

std::vector<DeviceInfo> parse_static_map(std::string_view text) {
    std::vector<DeviceInfo> out;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t semi = text.find(';', pos);
        const size_t end = semi == std::string_view::npos ? text.size() : semi;
        const std::string_view item = text.substr(pos, end - pos);
        pos = semi == std::string_view::npos ? text.size() : semi + 1;
        const auto eq = item.find('=');
        const auto colon = item.find(':');
        if (eq == std::string_view::npos || colon == std::string_view::npos
            || eq == 0 || colon < eq + 2 || colon + 2 > item.size()) {
            continue;  // 畸形条目跳过（Doc07 §7）
        }
        const auto port_str = item.substr(colon + 1);
        if (port_str.empty()
            || port_str.find_first_not_of("0123456789") != std::string_view::npos) {
            continue;
        }
        // 手工转端口：stoi 溢出抛异常，配置入口禁异常路径
        uint32_t port = 0;
        for (const char c : port_str) {
            port = port * 10 + static_cast<uint32_t>(c - '0');
            if (port > 65535) {
                port = 0;
                break;
            }
        }
        DeviceInfo dev;
        dev.alias.assign(item.substr(0, eq));
        dev.ip.assign(item.substr(eq + 1, colon - eq - 1));
        dev.port = static_cast<uint16_t>(port);
        if (!dev.alias.empty() && !dev.ip.empty() && dev.port != 0) {
            out.push_back(std::move(dev));
        }
    }
    return out;
}

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
    if (!self_alias_.empty() && dev.alias == self_alias_) {
        return;  // 收到自己的心跳（组播回环），不入列表（Doc07 §5）
    }
    const int64_t now = now_us();
    // 事件一律锁外发布：订阅方可能同步回查本服务（DeviceListModel），
    // 持锁 publish 会造成同线程对非递归 mutex 重入加锁（UB，已踩坑）
    Evt_DeviceOnline online_evt;
    Evt_AliasConflict conflict_evt;
    bool has_online = false;
    bool has_conflict = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        const auto it = table_.find(dev.alias);
        if (it == table_.end()) {
            DeviceInfo info = dev;
            info.last_seen_us = now;
            table_.emplace(dev.alias, info);
            SCLD_LOG(log_, LogLevel::Info, "discovery",
                     "[alias=" + dev.alias + "] 设备上线 ip=" + dev.ip);
            online_evt = make_online_evt(info);
            has_online = true;
        }
        else {
            // 别名冲突：同别名异 IP 且旧条目未超时（Doc07 §6.2）
            if (it->second.ip != dev.ip
                && now - it->second.last_seen_us < offline_timeout_us_) {
                Evt_AliasConflict e;
                e.device_alias = dev.alias;
                e.conflict_ip = dev.ip;
                e.timestamp_us = now;
                conflict_evt = e;
                has_conflict = true;
                SCLD_LOG(log_, LogLevel::Warn,
                         "discovery", "[alias=" + dev.alias + "] 别名冲突 old_ip="
                             + it->second.ip + " new_ip=" + dev.ip);
            }
            it->second.ip = dev.ip;
            it->second.port = dev.port;
            it->second.status = dev.status;
            it->second.version = dev.version;
            if (dev.code != 0) {
                it->second.code = dev.code;  // 旧包无码不覆盖已有值（Doc18 §3.4）
            }
            it->second.last_seen_us = now;
        }
    }
    if (has_online) {
        bus_->publish(online_evt);
    }
    else if (has_conflict) {
        bus_->publish(conflict_evt);
    }
}

bool AliasService::resolve(const std::string& alias, DeviceInfo& out) const {
    std::lock_guard<std::mutex> lk(mtx_);
    const auto it = table_.find(alias);
    if (it == table_.end()) {
        return false;
    }
    // 静态条目常在，不参与心跳超时
    if (static_set_.find(alias) == static_set_.end()
        && now_us() - it->second.last_seen_us > offline_timeout_us_) {
        return false;  // 已超时视为离线
    }
    out = it->second;
    return true;
}

std::vector<DeviceInfo> AliasService::online_devices() const {
    const int64_t now = now_us();
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<DeviceInfo> out;
    out.reserve(table_.size());
    for (const auto& [alias, info] : table_) {
        if (static_set_.find(alias) != static_set_.end()
            || now - info.last_seen_us <= offline_timeout_us_) {
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
            if (static_set_.find(it->first) != static_set_.end()) {
                ++it;  // 静态条目常在，永不过期
                continue;
            }
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
    // replace 语义：先清全部旧静态条目（热更新删除条目需生效），再插入新表
    for (const auto& alias : static_set_) {
        table_.erase(alias);
    }
    static_set_.clear();
    for (const auto& dev : table) {
        DeviceInfo info = dev;
        info.last_seen_us = now_us();  // 静态条目视为常在（Doc07 §7）
        table_.insert_or_assign(dev.alias, std::move(info));
        static_set_.insert(dev.alias);
    }
}

}  // namespace sc::localdesk
