// ============================================================
// scToLocalDesk | src/platform/linux/input_injector_linux.cpp
// XTest 键鼠注入（V1.0 豁免解除，Doc18 §8）：dlopen("libXtst.so.6")
// 运行时按需加载（无需构建期 dev 包），缺失/扩展不可用回退空注入器
// （InputService 空注入器静默不注入，服务不崩）。
// 键码映射：X keycode = Windows 扫描码 + 8（evdev 标准布局基线）；
// 鼠标：0~65535 归一化坐标按当前屏分辨率反算为绝对像素。
// 仅 Linux（exe 目标编入，不进 core 库）
// ============================================================
#include <dlfcn.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <algorithm>
#include <mutex>
#include <unordered_map>

#include "core/interface/iinput_injector.hpp"

namespace sc::localdesk {

namespace {

// libXtst 原型手工声明（规避构建期 libxtst-dev 依赖，签名 X11 标准稳定 ABI）
using FnQueryExt = Bool (*)(Display*, int*, int*, int*, int*);
using FnFakeKey = Bool (*)(Display*, unsigned int, Bool, unsigned long);
using FnFakeButton = Bool (*)(Display*, unsigned int, Bool, unsigned long);
using FnFakeMotion = Bool (*)(Display*, int, int, int, unsigned long);

struct XtstApi {
    void* so{nullptr};
    Display* dpy{nullptr};
    FnFakeKey fake_key{nullptr};
    FnFakeButton fake_button{nullptr};
    FnFakeMotion fake_motion{nullptr};
    int screen{0};
    uint32_t width{0};
    uint32_t height{0};

    ~XtstApi() {
        if (dpy != nullptr) {
            XCloseDisplay(dpy);
        }
        if (so != nullptr) {
            dlclose(so);
        }
    }

    /// 注入后立即冲刷：XTestFake* 只把请求排进 Xlib 客户端输出缓冲
    /// （~8KB），不 Flush 则攒满才发 XServer——200 注入/s 下积压 ~1.7s，
    /// 即"右键菜单半天才弹"的根因。XFlush 异步发送不等应答，开销可忽略。
    void flush_now() {
        XFlush(dpy);
    }
};

/// Windows VK → X keysym（US 布局，规则区 + 显式表）：
/// 控制端（Windows InputBridge）发的是 VK 码且 scan 恒 0，Linux 侧
/// 必须 VK→keysym→keycode 翻译——直接 scan+8 假设在此协议下不成立。
KeySym vk_to_keysym(uint16_t vk) {
    // 规则区：数字 0-9（XK_0..9 = 0x30..0x39 与 VK 同值）、字母（+0x20 小写）
    if (vk >= 0x30 && vk <= 0x39) {
        return static_cast<KeySym>(vk);
    }
    if (vk >= 0x41 && vk <= 0x5A) {
        return static_cast<KeySym>(vk + 0x20);
    }
    static const std::unordered_map<uint16_t, KeySym> kTable = {
        {0x08, XK_BackSpace}, {0x09, XK_Tab},       {0x0D, XK_Return},
        {0x10, XK_Shift_L},   {0x11, XK_Control_L}, {0x12, XK_Alt_L},
        {0x14, XK_Caps_Lock}, {0x1B, XK_Escape},    {0x20, XK_space},
        {0x21, XK_Prior},     {0x22, XK_Next},      {0x23, XK_End},
        {0x24, XK_Home},      {0x25, XK_Left},      {0x26, XK_Up},
        {0x27, XK_Right},     {0x28, XK_Down},      {0x2D, XK_Insert},
        {0x2E, XK_Delete},
        // OEM 标点（与控制端 kKeyTable 一一对应）
        {0xBA, XK_semicolon}, {0xBB, XK_equal},     {0xBC, XK_comma},
        {0xBD, XK_minus},     {0xBE, XK_period},    {0xBF, XK_slash},
        {0xC0, XK_grave},     {0xDB, XK_bracketleft}, {0xDC, XK_backslash},
        {0xDD, XK_bracketright}, {0xDE, XK_apostrophe},
    };
    const auto it = kTable.find(vk);
    if (it != kTable.end()) {
        return it->second;
    }
    if (vk >= 0x70 && vk <= 0x7B) {  // F1-F12
        return XK_F1 + static_cast<KeySym>(vk - 0x70);
    }
    return 0;  // 表外键（小键盘 OEM 等）丢弃——不注错键
}

/// 进程级单例（注入器工厂只建一次；X 显示连接与 so 句柄复用）
std::unique_ptr<XtstApi> g_api;
std::once_flag g_once;

bool init_api() {
    void* so = dlopen("libXtst.so.6", RTLD_NOW | RTLD_LOCAL);
    if (so == nullptr) {
        return false;
    }
    auto qe = reinterpret_cast<FnQueryExt>(dlsym(so, "XTestQueryExtension"));
    auto fk = reinterpret_cast<FnFakeKey>(dlsym(so, "XTestFakeKeyEvent"));
    auto fb = reinterpret_cast<FnFakeButton>(dlsym(so, "XTestFakeButtonEvent"));
    auto fm = reinterpret_cast<FnFakeMotion>(dlsym(so, "XTestFakeMotionEvent"));
    if (qe == nullptr || fk == nullptr || fb == nullptr || fm == nullptr) {
        dlclose(so);
        return false;
    }
    Display* dpy = XOpenDisplay(nullptr);
    if (dpy == nullptr) {
        dlclose(so);
        return false;
    }
    int ev = 0, er = 0, ma = 0, mi = 0;
    if (!qe(dpy, &ev, &er, &ma, &mi)) {
        XCloseDisplay(dpy);
        dlclose(so);
        return false;
    }
    const int screen = DefaultScreen(dpy);
    Screen* s = ScreenOfDisplay(dpy, screen);
    auto api = std::make_unique<XtstApi>();
    api->so = so;
    api->dpy = dpy;
    api->fake_key = fk;
    api->fake_button = fb;
    api->fake_motion = fm;
    api->screen = screen;
    api->width = static_cast<uint32_t>(s->width);
    api->height = static_cast<uint32_t>(s->height);
    g_api = std::move(api);
    return true;
}

class XTestInputInjector final : public IInputInjector {
public:
    bool inject_mouse(MouseAction action, int32_t x, int32_t y,
                      int32_t wheel_delta) override {
        XtstApi* api = g_api.get();
        if (api == nullptr) {
            return false;
        }
        // 归一化 0~65535 → 绝对像素（clamp 防越界 BadValue）
        const int px = std::clamp(static_cast<int>(
            static_cast<int64_t>(x) * api->width / 65535), 0,
            static_cast<int>(api->width) - 1);
        const int py = std::clamp(static_cast<int>(
            static_cast<int64_t>(y) * api->height / 65535), 0,
            static_cast<int>(api->height) - 1);
        unsigned int button = 0;
        Bool press = True;
        switch (action) {
        case MouseAction::Move:
            if (!api->fake_motion(api->dpy, api->screen, px, py, CurrentTime)) {
                return false;
            }
            api->flush_now();
            return true;
        case MouseAction::LeftDown:  button = Button1; break;
        case MouseAction::LeftUp:    button = Button1; press = False; break;
        case MouseAction::RightDown: button = Button3; break;
        case MouseAction::RightUp:   button = Button3; press = False; break;
        case MouseAction::MiddleDown: button = Button2; break;
        case MouseAction::MiddleUp:   button = Button2; press = False; break;
        case MouseAction::Wheel: {
            // X 纵向滚轮 = button 4/5 瞬时 press+release（无需配对语义）
            button = wheel_delta > 0 ? Button4 : Button5;
            const Bool ok = api->fake_button(api->dpy, button, True, CurrentTime)
                            && api->fake_button(api->dpy, button, False,
                                                CurrentTime);
            api->flush_now();
            return ok;
        }
        default:
            return false;
        }
        const Bool ok = api->fake_button(api->dpy, button, press, CurrentTime);
        api->flush_now();  // 立即发送（右键菜单即时弹出的关键）
        return ok;
    }

    bool inject_key(uint16_t vk, uint16_t /*scan_code*/, bool key_down) override {
        // 控制端协议只发 VK（scan 恒 0）：VK → keysym → keycode 翻译
        XtstApi* api = g_api.get();
        if (api == nullptr) {
            return false;
        }
        const KeySym ks = vk_to_keysym(vk);
        if (ks == 0) {
            return false;
        }
        const KeyCode kc = XKeysymToKeycode(api->dpy, ks);
        if (kc == 0) {
            return false;  // 该 keysym 本键盘无对应物理键
        }
        const Bool ok = api->fake_key(api->dpy, static_cast<unsigned int>(kc),
                                      key_down ? True : False, CurrentTime);
        api->flush_now();
        return ok;
    }
};

}  // namespace

}  // namespace sc::localdesk

// 工厂（exe 目标链接）；XTest 不可用回退 nullptr（InputService 空注入器静默）
namespace sc::localdesk {
std::shared_ptr<IInputInjector> make_input_injector() {
    std::call_once(g_once, init_api);
    if (g_api == nullptr) {
        return nullptr;
    }
    return std::make_shared<XTestInputInjector>();
}
}  // namespace sc::localdesk
