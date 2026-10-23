// ============================================================
// scToLocalDesk | src/platform/win32/auto_start.hpp
// 开机自启：HKCU\...\Run 注册表键（Doc17 app.auto_start）
// ============================================================
#pragma once

#include <string>

namespace sc::localdesk::platform {

/// enabled=false 时删除注册表键；返回是否成功
bool set_auto_start(bool enabled, const std::wstring& exe_path);

}  // namespace sc::localdesk::platform
