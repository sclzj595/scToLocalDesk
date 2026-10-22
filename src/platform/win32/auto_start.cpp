// ============================================================
// scToLocalDesk | src/platform/win32/auto_start.cpp
// 开机自启：CurrentUser Run 注册表键（Doc15 §3，无需管理员权限）
// ============================================================
#include <windows.h>

#include <string>

namespace sc::localdesk::platform {

namespace {
constexpr wchar_t kRunKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"scToLocalDesk";
}  // namespace

/// enabled=true 写入 exe 全路径；false 删除键值（Doc17 app.auto_start）
bool set_auto_start(bool enabled, const std::wstring& exe_path) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    const LSTATUS st = enabled
        ? RegSetValueExW(key, kValueName, 0, REG_SZ,
                         reinterpret_cast<const BYTE*>(exe_path.c_str()),
                         static_cast<DWORD>((exe_path.size() + 1) * sizeof(wchar_t)))
        : RegDeleteValueW(key, kValueName);
    RegCloseKey(key);
    return st == ERROR_SUCCESS;
}

}  // namespace sc::localdesk::platform
