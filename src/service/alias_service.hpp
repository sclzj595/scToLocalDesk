// ============================================================
// scToLocalDesk | src/service/alias_service.hpp
// 别名服务：设备表维护 / 别名解析 / 超时离线 / 冲突检测（Doc07 §1）
// 纯逻辑无 Qt；心跳收发在 Qt 适配器，结果经 upsert_device 注入
// ============================================================
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/interface/idiscovery.hpp"

namespace sc::localdesk::foundation {
class EventBus;
class ILogger;
}

namespace sc::localdesk {

/// 静态映射文本解析（Doc07 §7）：`alias=ip:port;...`，畸形条目跳过；
/// 全程无 throw（端口溢出/缺段直接丢弃），供装配层与设置页共用
std::vector<DeviceInfo> parse_static_map(std::string_view text);

/// 手动对端文本解析（Doc19 §6.3）：`alias|ip[:port];...`，port 缺省取
/// default_port（=network.ws_port）；IP 校验点分四段（0~255），畸形条目跳过，
/// 同名条目后者覆盖前者（去重）；alias 含 `;|=` 视为畸形。无 Qt 供冒烟直测
std::vector<DeviceInfo> parse_manual_peers(std::string_view text,
                                           uint16_t default_port);

class AliasService final {
public:
    AliasService(std::shared_ptr<foundation::EventBus> bus,
                 std::shared_ptr<foundation::ILogger> logger);

    /// 心跳注入（Qt 适配器解析 UDP 后调用）；输出在线/冲突事件
    void upsert_device(const DeviceInfo& dev);

    /// 别名解析（控制端发起协助前调用，Doc07 §1）
    /// 别名解析：未命中/已超时返回 false（out 保持不变）
    bool resolve(const std::string& alias, DeviceInfo& out) const;

    /// IT 视图在线列表
    std::vector<DeviceInfo> online_devices() const;

    /// 周期清理超时设备并发布离线事件（心跳线程调用）
    void cleanup_expired();

    /// 手动对端探活失败主动下线（Doc19 §6.3）：仅当该别名条目确为所探 ip
    /// 时移除并发布离线（防同名广播条目误杀）；条目不存在静默
    void mark_device_offline(const std::string& alias, const std::string& ip);

    void set_self_alias(std::string alias);
    void set_offline_timeout(int64_t timeout_us);
    void set_static_map(const std::vector<DeviceInfo>& table);

    const std::string& self_alias() const;

private:
    mutable std::mutex mtx_;
    std::unordered_map<std::string, DeviceInfo> table_;  ///< alias -> info
    std::unordered_set<std::string> static_set_;         ///< 静态条目豁免超时（Doc07 §7）
    std::string self_alias_;
    int64_t offline_timeout_us_{15'000'000};  ///< Doc17 network.offline_timeout_s
    std::shared_ptr<foundation::EventBus> bus_;
    std::shared_ptr<foundation::ILogger> log_;
};

}  // namespace sc::localdesk
