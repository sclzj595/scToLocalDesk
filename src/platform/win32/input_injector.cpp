// ============================================================
// scToLocalDesk | src/platform/win32/input_injector.cpp
// 键鼠注入实现：SendInput（Doc02 §5.2），普通用户权限可用
// ============================================================
#include <windows.h>

#include "core/interface/iinput_injector.hpp"

namespace sc::localdesk {

class Win32InputInjector final : public IInputInjector {
public:
    bool inject_mouse(MouseAction action, int32_t x, int32_t y,
                      int32_t wheel_delta) override {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dx = x;  // 归一化 0~65535（MOUSEEVENTF_ABSOLUTE）
        input.mi.dy = y;
        switch (action) {
        case MouseAction::Move:
            input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
            break;
        case MouseAction::LeftDown:
            input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            break;
        case MouseAction::LeftUp:
            input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            break;
        case MouseAction::RightDown:
            input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
            break;
        case MouseAction::RightUp:
            input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
            break;
        case MouseAction::MiddleDown:
            input.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
            break;
        case MouseAction::MiddleUp:
            input.mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
            break;
        case MouseAction::Wheel:
            input.mi.dwFlags = MOUSEEVENTF_WHEEL;
            input.mi.mouseData = static_cast<DWORD>(wheel_delta);
            break;
        default:
            return false;
        }
        return SendInput(1, &input, sizeof(INPUT)) == 1;
    }

    bool inject_key(uint16_t vk, uint16_t scan_code, bool key_down) override {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = vk;
        input.ki.wScan = scan_code;
        input.ki.dwFlags = key_down ? (scan_code != 0 ? KEYEVENTF_SCANCODE : 0)
                                    : KEYEVENTF_KEYUP | (scan_code != 0 ? KEYEVENTF_SCANCODE : 0);
        return SendInput(1, &input, sizeof(INPUT)) == 1;
    }
};

}  // namespace sc::localdesk

// 工厂（exe 目标链接）
namespace sc::localdesk {
std::shared_ptr<IInputInjector> make_input_injector() {
    return std::make_shared<Win32InputInjector>();
}
}  // namespace sc::localdesk
