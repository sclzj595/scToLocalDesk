// ============================================================
// scToLocalDesk | src/service/alias_service.hpp
// 别名服务：设备表维护 / 别名解析 / 超时离线 / 冲突检测（Doc07 §1）
// 纯逻辑无 Qt；心跳收发在 Qt 适配器，结果经 upsert_device 注入
// ============================================================
#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/interface/idiscovery.hpp"

namespace sc::localdesk::foundation {
class EventBus;
class ILogger;
}

namespace sc::localdesk {

class AliasService final {
public:
    AliasService(std::shared_ptr<foundation::EventBus> bus,
                 std::shared_ptr<foundation::ILogger> logger);

    /// 心跳注入（Qt 适配器解析 UDP 后调用）；输出在线/冲突事件
    void upsert_device(const DeviceInfo& dev);

    /// 别名解析（控制端发起协助前调用，Doc07 §1）
    [[nodiscard]] std::optional<DeviceInfo> resolve(const std::string& alias) const;

    /// IT 视图在线列表
    [[nodiscard]] std::vector<DeviceInfo> online_devices() const;

    /// 周期清理超时设备并发布离线事件（心跳线程调用）
    void cleanup_expired();

    void set_self_alias(std::string alias);
    void set_offline_timeout(int64_t timeout_us);
    void set_static_map(const std::vector<DeviceInfo>& table);

    [[nodiscard]] const std::string& self_alias() const;

private:
    mutable std::mutex mtx_;
    std::unordered_map<std::string, DeviceInfo> table_;  ///< alias -> info
    std::string self_alias_;
    int64_t offline_timeout_us_{15'000'000};  ///< Doc17 network.offline_timeout_s
    std::shared_ptr<foundation::EventBus> bus_;
    std::shared_ptr<foundation::ILogger> log_;
};

}  // namespace sc::localdesk
