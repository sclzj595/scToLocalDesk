// ============================================================
// scToLocalDesk | src/platform/win32/crash_reporter.cpp
// MiniDump 实现：dbghelp 链接 + 异常点现场抓取
// 注意：过滤回调内禁用 CRT/日志/STL 容器分配（崩溃态堆可能已损坏），
// 仅 Win32 API + 栈上缓冲 + 独立 FILE* 写盘
// ============================================================
#include "platform/win32/crash_reporter.hpp"

#ifdef _WIN32

    #include <windows.h>
    #include <dbghelp.h>

    #include <cstdio>

namespace sc::localdesk::platform {
namespace {

std::filesystem::path g_dump_dir;

LONG WINAPI unhandled_filter(EXCEPTION_POINTERS* info) {
    // 时间戳目录名（CRT 只用 strftime 栈缓冲，不碰堆）
    SYSTEMTIME st;
    GetLocalTime(&st);
    char name[64] = {};
    sprintf_s(name, sizeof(name),
              "scld_crash_%04u%02u%02u_%02u%02u%02u_%lu",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
              GetCurrentProcessId());

    if (!g_dump_dir.empty()) {
        CreateDirectoryW(g_dump_dir.c_str(), nullptr);
    }

    // dump 路径：栈上拼宽字符（路径前缀 ≤ MAX_PATH 假定成立，exe 旁相对）
    wchar_t dump_path[MAX_PATH] = {};
    if (g_dump_dir.empty()) {
        GetModuleFileNameW(nullptr, dump_path, MAX_PATH);
        wchar_t* slash = wcsrchr(dump_path, L'\\');
        if (slash != nullptr) {
            *slash = L'\0';
        }
    }
    else {
        wcsncpy_s(dump_path, g_dump_dir.c_str(), _TRUNCATE);
    }
    wchar_t file_base[MAX_PATH] = {};
    swprintf_s(file_base, L"%s\\%S", dump_path, name);

    // 1) MiniDump 全现场（含线程栈/句柄表，WinDbg 直接打开）
    HANDLE proc = GetCurrentProcess();
    HANDLE tid = GetCurrentThread();
    MINIDUMP_EXCEPTION_INFORMATION mei;
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = info;
    mei.ClientPointers = FALSE;

    wchar_t dmp[MAX_PATH] = {};
    swprintf_s(dmp, L"%s.dmp", file_base);
    HANDLE f = CreateFileW(dmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        MiniDumpWriteDump(proc, GetCurrentProcessId(), f, MiniDumpNormal, &mei,
                          nullptr, nullptr);
        CloseHandle(f);
    }

    // 2) 伴随文本：异常码/地址（人读首屏，免开 WinDbg 即可分诊）
    wchar_t txt[MAX_PATH] = {};
    swprintf_s(txt, L"%s.txt", file_base);
    HANDLE tf = CreateFileW(txt, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (tf != INVALID_HANDLE_VALUE) {
        char line[128] = {};
        const DWORD code = info != nullptr && info->ExceptionRecord != nullptr
                               ? info->ExceptionRecord->ExceptionCode
                               : 0;
        const void* addr = info != nullptr && info->ExceptionRecord != nullptr
                               ? info->ExceptionRecord->ExceptionAddress
                               : nullptr;
        sprintf_s(line, sizeof(line),
                  "crash code=0x%08lX addr=%p pid=%lu\r\n",
                  code, addr, GetCurrentProcessId());
        DWORD written = 0;
        WriteFile(tf, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
        CloseHandle(tf);
    }
    return EXCEPTION_EXECUTE_HANDLER;  // 交还系统默认弹窗/退出码
}

}  // namespace

bool CrashReporter::install(const std::filesystem::path& dump_dir) {
    g_dump_dir = dump_dir;
    return SetUnhandledExceptionFilter(unhandled_filter) != nullptr
           || GetLastError() == ERROR_SUCCESS;
}

void CrashReporter::crash_for_test() {
    // NPE：MSVC /GS 不拦的显式空解引用，稳定触发 0xC0000005
    volatile int* p = nullptr;
    *p = 1;
}

}  // namespace sc::localdesk::platform

#else
// ============================================================
// Linux（Doc18 §8-3）：崩溃现场由系统 core dump 承接
// （systemd-coredump / ulimit -c unlimited；apport 落
// /var/crash），进程内不做崩溃态采集——崩溃态堆不可信。
// install 仅确保目录存在（dump_dir 供用户从 core 文件归档）。
// ============================================================
#include <cstdio>

namespace sc::localdesk::platform {

bool CrashReporter::install(const std::filesystem::path& dump_dir) {
    std::error_code ec;
    std::filesystem::create_directories(dump_dir, ec);
    return !ec;
}

void CrashReporter::crash_for_test() {
    // NPE：SIGSEGV，交系统 core dump 承接（进程无自定义 handler）
    volatile int* p = nullptr;
    *p = 1;
}

}  // namespace sc::localdesk::platform

#endif  // _WIN32
