// ============================================================
// scToLocalDesk | src/platform/win32/session_lock.cpp
// OpenInputDesktop 探测：无需 WTS 注册窗口，GUI 进程用户会话内
// 调用即准确；锁屏/Winlogon/UAC 安全桌面一律拒绝打开
// ============================================================
#include "platform/win32/session_lock.hpp"

#include <windows.h>

namespace sc::localdesk::platform {

bool is_session_locked() {
    // 打开 input desktop：锁屏/安全桌面 → 失败（拒绝访问）；
    // 成功则用完即关，无资源滞留
    HDESK desk = ::OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (desk != nullptr) {
        ::CloseDesktop(desk);
        return false;
    }
    return true;
}

}  // namespace sc::localdesk::platform
