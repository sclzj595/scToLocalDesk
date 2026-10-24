// ============================================================
// scToLocalDesk | src/platform/linux/system_info_linux.cpp
// Linux 实现镜像 platform/win32/system_info（Doc18 P6 提前量）
// hostname/username 走 POSIX；MAC 取首块非 lo 的上行网卡
// ============================================================
#include "platform/win32/system_info.hpp"

#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <ifaddrs.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <string>

namespace sc::localdesk::platform {

SystemInfo query_system_info() {
    SystemInfo info;

    char host[256] = {};
    if (gethostname(host, sizeof(host) - 1) == 0) {
        info.hostname = host;
    }

    // username：SUDO_USER 优先（sudo 提权场景取真实用户），回落 getpwuid
    if (const char* sudo_user = std::getenv("SUDO_USER"); sudo_user != nullptr) {
        info.username = sudo_user;
    }
    else if (const char* user = std::getenv("USER"); user != nullptr) {
        info.username = user;
    }

    // MAC：getifaddrs 取首块非 lo 且 up 的网卡硬件地址（12 位大写 hex）
    ifaddrs* ifs = nullptr;
    if (getifaddrs(&ifs) == 0 && ifs != nullptr) {
        for (const ifaddrs* it = ifs; it != nullptr; it = it->ifa_next) {
            if (it->ifa_addr == nullptr
                || it->ifa_addr->sa_family != AF_PACKET
                || (it->ifa_flags & IFF_LOOPBACK) != 0
                || (it->ifa_flags & IFF_UP) == 0) {
                continue;
            }
            const sockaddr_ll* sll = reinterpret_cast<const sockaddr_ll*>(it->ifa_addr);
            if (sll->sll_halen != 6) {
                continue;
            }
            char mac[13] = {};
            std::snprintf(mac, sizeof(mac),
                          "%02X%02X%02X%02X%02X%02X",
                          sll->sll_addr[0], sll->sll_addr[1], sll->sll_addr[2],
                          sll->sll_addr[3], sll->sll_addr[4], sll->sll_addr[5]);
            info.mac = mac;
            break;
        }
        freeifaddrs(ifs);
    }
    return info;
}

}  // namespace sc::localdesk::platform
