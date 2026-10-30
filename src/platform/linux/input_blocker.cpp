// ============================================================
// scToLocalDesk | src/platform/linux/input_blocker.cpp
// 冻结键鼠输入（Doc19 §2.1）：XGrabKeyboard + XGrabPointer。
// exe 目标已链 X11（CMake SCLD_X11），直接调用无需 dlopen
//（dlopen 先例用于可选组件 libXtst，此处 X11 为既有硬依赖）。
// 抓取期间事件由 X server 重定向给本进程、读都不读即丢弃——
// 双向锁语义（物理+XTest 注入全丢，画面继续传）。
// 仅 Linux（exe 目标编入，不进 core 库）
// ============================================================
#include "platform/linux/input_blocker.hpp"

#include <X11/Xlib.h>

#include <atomic>
#include <mutex>
#include <utility>

namespace sc::localdesk::platform {

namespace {

std::atomic<bool> g_frozen{false};
std::mutex g_mtx;       // 保护 disp_（托盘勾选与自动解除都在主线程，仅防御）
Display* g_disp{nullptr};

bool grab_all() {
    Display* dpy = XOpenDisplay(nullptr);
    if (dpy == nullptr) {
        return false;
    }
    Window root = DefaultRootWindow(dpy);
    XSync(dpy, False);
    // 抓键鼠：owner_events=False（事件全归本进程，读即丢）；
    // Async/Async 不冻结指针设备本身，事件流被本连接独占
    const int kb = XGrabKeyboard(dpy, root, False, GrabModeAsync, GrabModeAsync,
                                 CurrentTime);
    const int ms = XGrabPointer(dpy, root, False, 0, GrabModeAsync, GrabModeAsync,
                                None, None, CurrentTime);
    XFlush(dpy);
    if (kb != GrabSuccess || ms != GrabSuccess) {
        if (kb == GrabSuccess) {
            XUngrabKeyboard(dpy, CurrentTime);
        }
        if (ms == GrabSuccess) {
            XUngrabPointer(dpy, CurrentTime);
        }
        XFlush(dpy);
        XCloseDisplay(dpy);
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_disp = dpy;
    }
    return true;
}

void ungrab_all() {
    Display* dpy = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        dpy = g_disp;
        g_disp = nullptr;
    }
    if (dpy == nullptr) {
        return;
    }
    XUngrabKeyboard(dpy, CurrentTime);
    XUngrabPointer(dpy, CurrentTime);
    XFlush(dpy);
    XCloseDisplay(dpy);
}

}  // namespace

bool set_input_frozen(bool on) {
    if (on == g_frozen.load(std::memory_order_relaxed)) {
        return g_frozen.load(std::memory_order_relaxed) || !on;
    }
    if (on) {
        if (!grab_all()) {
            return false;
        }
        g_frozen.store(true, std::memory_order_release);
    }
    else {
        ungrab_all();
        g_frozen.store(false, std::memory_order_release);
    }
    return true;
}

bool is_input_frozen() {
    return g_frozen.load(std::memory_order_acquire);
}

void set_freeze_changed_callback(std::function<void(bool)> cb) {
    (void)cb;  // Linux 无钩子内自解路径（预留对齐 Windows 版接口）
}

}  // namespace sc::localdesk::platform
