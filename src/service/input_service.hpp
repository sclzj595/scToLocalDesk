// ============================================================
// scToLocalDesk | src/service/input_service.hpp
// 被控端键鼠分派：MOUSE_KEY 帧 → IInputInjector 注入（Doc02 §5.2）
// header-only；线程安全由 injector 实现保证（SendInput 原子）
// ============================================================
#pragma once

#include <memory>

#include "core/interface/iinput_injector.hpp"
#include "core/protocol/tlv.hpp"

namespace sc::localdesk {

class InputService final {
public:
    void set_injector(std::shared_ptr<IInputInjector> inj) { inj_ = std::move(inj); }

    /// 会话服务 Running 态分派入口；body 为 MOUSE_KEY 报文体
    bool handle(const uint8_t* body, size_t len) {
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
};

}  // namespace sc::localdesk
