// ============================================================
// scToLocalDesk | src/service/input_service.hpp
// 被控端键鼠分派：MOUSE_KEY 帧 → IInputInjector 注入（Doc02 §5.2）
// header-only；线程安全由 injector 实现保证（SendInput 原子）
// ============================================================
#pragma once

#include <atomic>
#include <memory>

#include "core/interface/iinput_injector.hpp"
#include "core/protocol/tlv.hpp"
#include "foundation/logger/ilogger.hpp"

namespace sc::localdesk {

class InputService final {
public:
    void set_injector(std::shared_ptr<IInputInjector> inj) { inj_ = std::move(inj); }

    /// 只读模式开关（被控端审批未授权键鼠时置位，RemoteSessionService 推入）：
    /// 置位后所有 MOUSE_KEY 帧丢弃不注入；控制端本地仍采集发送（省协议改动），
    /// 由被控端单侧丢弃兜底
    void set_read_only(bool ro) { read_only_.store(ro, std::memory_order_relaxed); }

    /// 日志注入（可空）：只读丢弃首帧 Debug 一次去重用；
    /// RemoteSessionService::set_input_service 时自动传入
    void set_logger(foundation::ILogger* lg) { log_ = lg; }

    /// 会话服务 Running 态分派入口；body 为 MOUSE_KEY 报文体
    bool handle(const uint8_t* body, size_t len) {
        if (read_only_.load(std::memory_order_relaxed)) {
            // 只读模式：丢弃不注入。返回 true（视为已处理），
            // 防上层误报「注入失败」Warn；日志 Debug 一次去重防刷屏
            if (!ro_drop_logged_.exchange(true, std::memory_order_relaxed)) {
                SCLD_LOG(log_, foundation::LogLevel::Debug, "input",
                         "只读模式：丢弃键鼠事件（对方未授权键鼠控制）");
            }
            return true;
        }
        if (inj_ == nullptr) {
            return false;
        }
        protocol::MouseKeyBody mk;
        if (!protocol::parse_mouse_key(body, len, mk)) {
            return false;  // 畸形帧丢弃
        }
        if (mk.kind == protocol::MouseKeyKind::Mouse) {
            return inj_->inject_mouse(static_cast<MouseAction>(mk.action), mk.x, mk.y, mk.wheel);
        }
        return inj_->inject_key(mk.vk, mk.scan, mk.down != 0);
    }

private:
    std::shared_ptr<IInputInjector> inj_;
    std::atomic<bool> read_only_{false};   ///< 只读开关（Running 期由会话服务写入）
    std::atomic<bool> ro_drop_logged_{false};  ///< 只读丢弃日志去重（一次性）
    foundation::ILogger* log_{nullptr};    ///< 弱引用（装配层保证存活期覆盖服务）
};

}  // namespace sc::localdesk
