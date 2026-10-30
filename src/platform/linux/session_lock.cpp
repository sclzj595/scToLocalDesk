// ============================================================
// scToLocalDesk | src/platform/linux/session_lock.cpp
// D-Bus 查询实现（Ubuntu 22.04 GNOME 基准）：org.freedesktop.ScreenSaver
// 是 freedesktop 标准席位锁接口，gnome-shell 提供；不引 D-Bus 库依赖，
// 子进程一次查询（锁屏检测仅发生在入站请求瞬间，开销可忽略）
// ============================================================
#include "platform/linux/session_lock.hpp"

#include <cstdio>
#include <string>

namespace sc::localdesk::platform {

bool is_session_locked() {
    // --session 总线 + GetActive → "boolean true/false"；
    // stderr 静默（服务缺失时 dbus-send 报错属正常探测失败）
    FILE* fp = ::popen(
        "dbus-send --session --print-reply "
        "--dest=org.freedesktop.ScreenSaver /org/freedesktop/ScreenSaver "
        "org.freedesktop.ScreenSaver.GetActive 2>/dev/null",
        "r");
    if (fp != nullptr) {
        char buf[256] = {};
        std::string out;
        while (::fgets(buf, sizeof(buf), fp) != nullptr) {
            out += buf;
        }
        ::pclose(fp);
        if (out.find("boolean true") != std::string::npos) {
            return true;
        }
        if (out.find("boolean false") != std::string::npos) {
            return false;
        }
        // 输出异常（接口缺失等）→ 落到 fail-open
    }
    return false;
}

}  // namespace sc::localdesk::platform
