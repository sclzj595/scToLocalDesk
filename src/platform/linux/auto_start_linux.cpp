// ============================================================
// scToLocalDesk | src/platform/linux/auto_start_linux.cpp
// Linux 自启动占位（签名同 win32/auto_start.hpp）
// P6 落位：~/.config/autostart/*.desktop（XDG 规范）；
// 当前返回 false 不静默成功——设置页写入会收到失败反馈
// ============================================================
#include "platform/win32/auto_start.hpp"

namespace sc::localdesk::platform {

bool set_auto_start(bool enabled, const std::wstring& exe_path) {
    (void)enabled;
    (void)exe_path;
    return false;  // P6: XDG autostart .desktop 实现落位处
}

}  // namespace sc::localdesk::platform
