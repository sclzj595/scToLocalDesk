// ============================================================
// scToLocalDesk | src/platform/linux/input_injector_linux.cpp
// Linux 注入器占位（接口同 iinput_injector.hpp）
// V1.0 被控端为 Windows 基线；Linux 被控（uinput/XTest）为 P6 任务，
// 当前返回 nullptr —— InputService 空注入器时静默不注入（服务不崩）
// ============================================================
#include "core/interface/iinput_injector.hpp"

namespace sc::localdesk {

std::shared_ptr<IInputInjector> make_input_injector() {
    return nullptr;  // P6: uinput（Wayland）/ XTest（X11）实现落位处
}

}  // namespace sc::localdesk
