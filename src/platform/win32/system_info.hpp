// ============================================================
// scToLocalDesk | src/platform/win32/system_info.hpp
// 系统身份采集：主机名 / 用户名 / 首块非虚拟网卡 MAC（Doc18 §3.1）
// 宽字符 Win32 API 统一收敛于此，输出 UTF-8 窄串供 service 层消费
// ============================================================
#pragma once

#include <string>

namespace sc::localdesk::platform {

struct SystemInfo {
    std::string hostname;  // UTF-8；API 失败时为空
    std::string username;  // UTF-8；API 失败时为空
    std::string mac;       // 12 位大写 hex 无分隔符；无非虚拟在线网卡时为空
};

/// 采集本机身份三要素（GetComputerNameW/GetUserNameW/GetAdaptersAddresses）
SystemInfo query_system_info();

}  // namespace sc::localdesk::platform
