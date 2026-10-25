// ============================================================
// scToLocalDesk | src/platform/win32/system_info.cpp
// Win32 实现：宽字符 API + 网卡过滤（回环/未连接/虚拟关键字）
// ============================================================
#include "platform/win32/system_info.hpp"

// iphlpapi 依赖 winsock 类型（SOCKET_ADDRESS 等），必须在 windows.h 之前
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <iphlpapi.h>

#include <cwctype>
#include <vector>

namespace sc::localdesk::platform {

namespace {

std::string wide_to_utf8(const wchar_t* w, size_t len) {
    if (w == nullptr || len == 0) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len),
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), out.data(), n,
                        nullptr, nullptr);
    return out;
}

bool is_virtual_adapter(const wchar_t* friendly_name) {
    std::wstring s(friendly_name == nullptr ? L"" : friendly_name);
    for (auto& c : s) {
        c = static_cast<wchar_t>(::towlower(c));
    }
    // 虚拟网卡家族关键字：VMware/VBox/Hyper-V/WSL/TAP/VPN/蓝牙/回环
    constexpr const wchar_t* kVirtualKeys[] = {
        L"vmware", L"virtualbox", L"vbox", L"hyper-v", L"vethernet",
        L"virtual", L"loopback", L"wsl", L"tap", L"vpn", L"bluetooth",
        L"wan miniport",
    };
    for (const auto* key : kVirtualKeys) {
        if (s.find(key) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

/// 首块在线非虚拟网卡的 MAC，12 位大写 hex；无则空串
std::string first_physical_mac() {
    constexpr ULONG kFlags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST
                           | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG size = 15 * 1024;
    std::vector<unsigned char> buf(size);
    for (;;) {
        const ULONG ret = GetAdaptersAddresses(
            AF_UNSPEC, kFlags, nullptr,
            reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
        if (ret == ERROR_BUFFER_OVERFLOW) {
            buf.assign(size, 0);
            continue;
        }
        if (ret != NO_ERROR) {
            return {};
        }
        break;
    }
    for (auto* ad = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
         ad != nullptr; ad = ad->Next) {
        if (ad->IfType == IF_TYPE_SOFTWARE_LOOPBACK
            || ad->OperStatus != IfOperStatusUp
            || ad->PhysicalAddressLength != 6
            || is_virtual_adapter(ad->FriendlyName)) {
            continue;
        }
        static constexpr char kHex[] = "0123456789ABCDEF";
        char hex[13] = {};
        for (ULONG i = 0; i < ad->PhysicalAddressLength; ++i) {
            hex[i * 2] = kHex[ad->PhysicalAddress[i] >> 4];
            hex[i * 2 + 1] = kHex[ad->PhysicalAddress[i] & 0x0F];
        }
        return hex;
    }
    return {};
}

/// 全部在线网卡 IPv4 字符串（含虚拟网卡，排回环）：自连拦截比对集合
/// PRD：只拦"自己连自己"，虚拟机/同网段设备不在集合内正常放行
std::vector<std::string> all_local_ipv4() {
    std::vector<std::string> out;
    constexpr ULONG kFlags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST
                           | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG size = 15 * 1024;
    std::vector<unsigned char> buf(size);
    for (;;) {
        const ULONG ret = GetAdaptersAddresses(
            AF_INET, kFlags, nullptr,
            reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
        if (ret == ERROR_BUFFER_OVERFLOW) {
            buf.assign(size, 0);
            continue;
        }
        if (ret != NO_ERROR) {
            return out;
        }
        break;
    }
    for (auto* ad = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
         ad != nullptr; ad = ad->Next) {
        if (ad->IfType == IF_TYPE_SOFTWARE_LOOPBACK
            || ad->OperStatus != IfOperStatusUp) {
            continue;
        }
        for (auto* un = ad->FirstUnicastAddress; un != nullptr; un = un->Next) {
            if (un->Address.lpSockaddr == nullptr
                || un->Address.lpSockaddr->sa_family != AF_INET) {
                continue;
            }
            const auto* sa =
                reinterpret_cast<const sockaddr_in*>(un->Address.lpSockaddr);
            char ip[INET_ADDRSTRLEN] = {};
            if (inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip)) != nullptr) {
                out.push_back(ip);
            }
        }
    }
    return out;
}

}  // namespace

SystemInfo query_system_info() {
    SystemInfo si;
    wchar_t host[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD len = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(host, &len)) {
        si.hostname = wide_to_utf8(host, len);
    }
    wchar_t user[256] = {};
    DWORD ulen = 256;
    if (GetUserNameW(user, &ulen) && ulen > 1) {
        si.username = wide_to_utf8(user, ulen - 1);  // ulen 含结尾符
    }
    si.mac = first_physical_mac();
    si.ips = all_local_ipv4();
    return si;
}

}  // namespace sc::localdesk::platform
