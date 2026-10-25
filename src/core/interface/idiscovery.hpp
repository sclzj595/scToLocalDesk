// ============================================================
// scToLocalDesk | src/core/interface/idiscovery.hpp
// 设备发现抽象契约：UDP 组播 / 静态映射双模式（Doc06 §2 / Doc07）
// ============================================================
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/interface/types.hpp"

namespace sc::localdesk {

class IDiscovery {
public:
    virtual ~IDiscovery() = default;

    virtual bool start() = 0;
    virtual void stop() = 0;

    /// 别名解析：alias -> ip:port；未命中返回 nullopt（Doc07 §1）
    virtual std::optional<DeviceInfo> resolve(const std::string& alias) = 0;

    /// IT 视图在线列表（已过滤超时）
    virtual std::vector<DeviceInfo> online_devices() const = 0;

    /// 静态映射模式（multicast_enabled=false）：加载配置表（Doc07 §7）
    virtual void set_static_map(const std::vector<DeviceInfo>& table) = 0;
};

}  // namespace sc::localdesk
