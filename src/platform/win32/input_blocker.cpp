// ============================================================
// scToLocalDesk | src/platform/win32/input_blocker.cpp
// 冻结键鼠输入（Doc19 §2.1「会话内一键冻结输入」）：低级钩子实现。
// 双向锁定稿：物理+注入事件全丢（SendInput 注入同样过 LL 钩子），
// 画面继续传——语义为「冻结输入、画面继续」。不用 BlockInput：
// 其锁死包括本进程在内的一切输入且无进程内出路。
// 逃生通道（三重）：自家进程窗口放行（托盘取消勾选解除）/
// Ctrl+Alt+Shift+F12 钩子内直接解除 / Ctrl+Alt+Del 系统级天然不受钩子影响。
// 仅 exe 目标编入（钩子须装在有消息循环的线程——Qt 主线程）
// ============================================================
#include "platform/win32/input_blocker.hpp"

#include <windows.h>

#include <atomic>
#include <utility>

namespace sc::localdesk::platform {

namespace {

std::atomic<bool> g_frozen{false};
HHOOK g_kb_hook{nullptr};
HHOOK g_mouse_hook{nullptr};
std::function<void(bool)> g_changed_cb;  // 主线程专用（钩子回调亦在主线程）

// Ctrl/Alt/Shift 按下态（LL 键盘钩子自行跟踪，快捷键判据）
bool g_mod_ctrl{false};
bool g_mod_alt{false};
bool g_mod_shift{false};

constexpr int kEscapeVk = VK_F12;  // 逃生组合键主键：Ctrl+Alt+Shift+F12

bool is_self_window(HWND hwnd) {
    if (hwnd == nullptr) {
        return false;
    }
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    return pid == ::GetCurrentProcessId();
}

/// 指点是否落在任务栏/托盘悬浮窗内（托盘图标宿主是 Explorer 的
/// Shell_TrayWnd/Shell_SecondaryTrayWnd/NotifyIconOverflowWindow——
/// 不放行则冻结期点不开托盘菜单，勾选解除通道失效）
bool pt_in_tray_area(const POINT& pt) {
    const wchar_t* kClasses[] = {
        L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"NotifyIconOverflowWindow"};
    for (const wchar_t* cls : kClasses) {
        const HWND hwnd = ::FindWindowW(cls, nullptr);
        if (hwnd == nullptr) {
            continue;
        }
        RECT rc{};
        if (!::GetWindowRect(hwnd, &rc)) {
            continue;
        }
        if (::PtInRect(&rc, pt)) {
            return true;
        }
    }
    return false;
}

void track_modifiers(const KBDLLHOOKSTRUCT& info, bool down) {
    switch (info.vkCode) {
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_CONTROL:
        g_mod_ctrl = down;
        break;
    case VK_LMENU:
    case VK_RMENU:
    case VK_MENU:
        g_mod_alt = down;
        break;
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_SHIFT:
        g_mod_shift = down;
        break;
    default:
        break;
    }
}

/// 快捷键自解（钩子内直接解除——本钩子回调运行在安装线程=Qt 主线程，
/// 直接同步回执托盘勾选态，无跨线程问题）
void unfreeze_in_hook() {
    g_frozen.store(false, std::memory_order_release);
    if (g_kb_hook != nullptr) {
        ::UnhookWindowsHookEx(g_kb_hook);
        g_kb_hook = nullptr;
    }
    if (g_mouse_hook != nullptr) {
        ::UnhookWindowsHookEx(g_mouse_hook);
        g_mouse_hook = nullptr;
    }
    if (g_changed_cb) {
        g_changed_cb(false);
    }
}

LRESULT CALLBACK kb_proc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && g_frozen.load(std::memory_order_relaxed)) {
        const auto& info = *reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);
        const bool down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
        if (down && info.vkCode == kEscapeVk && g_mod_ctrl && g_mod_alt
            && g_mod_shift) {
            unfreeze_in_hook();
            return 1;  // 吞掉 F12 本身，不泄漏给前台应用
        }
        track_modifiers(info, down);
        // 前台窗口为本进程（托盘菜单/主窗）时放行——勾选解除通道；
        // 其余（物理+注入）一律丢弃
        if (!is_self_window(::GetForegroundWindow())) {
            return 1;
        }
    }
    return ::CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT CALLBACK mouse_proc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && g_frozen.load(std::memory_order_relaxed)) {
        const auto& info = *reinterpret_cast<MSLLHOOKSTRUCT*>(lp);
        // 点在自家进程窗口（弹出后的托盘菜单/主窗）或任务栏托盘区放行；
        // 其余一律丢弃
        HWND under = ::WindowFromPoint(info.pt);
        const HWND root = under != nullptr ? ::GetAncestor(under, GA_ROOT) : nullptr;
        if (!is_self_window(root != nullptr ? root : under)
            && !pt_in_tray_area(info.pt)) {
            return 1;
        }
    }
    return ::CallNextHookEx(nullptr, code, wp, lp);
}

}  // namespace

bool set_input_frozen(bool on) {
    if (on == g_frozen.load(std::memory_order_relaxed)) {
        return g_frozen.load(std::memory_order_relaxed) || !on;
    }
    if (on) {
        // 先装钩子后置位（钩子回调读 g_frozen；未置位时钩子直通零开销）
        // LL 钩子须在带消息循环线程安装；失败常见于权限/会话隔离
        const HHOOK kb = ::SetWindowsHookExW(WH_KEYBOARD_LL, kb_proc, nullptr, 0);
        const HHOOK ms = ::SetWindowsHookExW(WH_MOUSE_LL, mouse_proc, nullptr, 0);
        if (kb == nullptr || ms == nullptr) {
            if (kb != nullptr) {
                ::UnhookWindowsHookEx(kb);
            }
            if (ms != nullptr) {
                ::UnhookWindowsHookEx(ms);
            }
            return false;
        }
        g_kb_hook = kb;
        g_mouse_hook = ms;
        g_mod_ctrl = g_mod_alt = g_mod_shift = false;
        g_frozen.store(true, std::memory_order_release);
    }
    else {
        g_frozen.store(false, std::memory_order_release);
        if (g_kb_hook != nullptr) {
            ::UnhookWindowsHookEx(g_kb_hook);
            g_kb_hook = nullptr;
        }
        if (g_mouse_hook != nullptr) {
            ::UnhookWindowsHookEx(g_mouse_hook);
            g_mouse_hook = nullptr;
        }
    }
    return true;
}

bool is_input_frozen() {
    return g_frozen.load(std::memory_order_acquire);
}

void set_freeze_changed_callback(std::function<void(bool)> cb) {
    g_changed_cb = std::move(cb);
}

}  // namespace sc::localdesk::platform
