// ============================================================
// scToLocalDesk | src/core/interface/iinput_injector.hpp
// 键鼠透传注入抽象契约（Doc02 §5.2）：被控端把远端键鼠事件注入本机
// 实现：platform/win32/input_injector（SendInput）
// ============================================================
#pragma once

#include <cstdint>
#include <memory>

namespace sc::localdesk {

enum class MouseAction : uint8_t {
    Move,       ///< 绝对坐标移动（0~65535 归一化，SendInput MOUSEEVENTF_ABSOLUTE）
    LeftDown,
    LeftUp,
    RightDown,
    RightUp,
    MiddleDown,
    MiddleUp,
    Wheel,      ///< delta：120 = 一格
};

class IInputInjector {
public:
    virtual ~IInputInjector() = default;

    /// 注入鼠标事件；坐标为 0~65535 归一化值（控制端按画面缩放反算，Doc10 §4）
    virtual bool inject_mouse(MouseAction action, int32_t x, int32_t y, int32_t wheel_delta) = 0;

    /// 注入键盘事件（Windows 虚拟键码 + 扫描码）
    virtual bool inject_key(uint16_t vk, uint16_t scan_code, bool key_down) = 0;
};

/// 工厂：平台注入器（win32 = SendInput；仅 WIN32 目标链接实现）
std::shared_ptr<IInputInjector> make_input_injector();

}  // namespace sc::localdesk
