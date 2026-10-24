// ============================================================
// scToLocalDesk | src/foundation/fs_portable.hpp
// 跨平台文件打开：Windows 走宽字符 _wfopen_s（中文安装路径安全），
// POSIX 窄 UTF-8（libstdc++ path::c_str 原生 UTF-8）
// ============================================================
#pragma once

#include <cstdio>
#include <cstring>
#include <filesystem>

namespace sc::localdesk::foundation {

/// 等价 fopen 的跨平台封装；失败返回 nullptr（模式串仅限 "rb"/"wb" 等窄字符）
inline std::FILE* fs_open(const std::filesystem::path& p, const char* mode) {
#ifdef _WIN32
    std::FILE* fp = nullptr;
    const std::wstring wmode(mode, mode + std::strlen(mode));
    if (_wfopen_s(&fp, p.wstring().c_str(), wmode.c_str()) != 0) {
        return nullptr;
    }
    return fp;
#else
    return std::fopen(p.c_str(), mode);
#endif
}

}  // namespace sc::localdesk::foundation
