// ============================================================
// scToLocalDesk | src/platform/win32/crash_reporter.hpp
// 崩溃捕获（Doc18 §6.2）：SetUnhandledExceptionFilter → MiniDump
// 写 exe 旁 dumps/scld_crash_时间戳_pid.dmp，伴随 .txt 记录异常码/地址
// 崩溃前最后一条 WARN/ERROR 由 spdlog flush_on(Warn) 保证已在盘外
// ============================================================
#pragma once

#include <filesystem>

namespace sc::localdesk::platform {

class CrashReporter final {
public:
    CrashReporter() = delete;

    /// 注册未处理异常过滤器；dump 落 dump_dir（缺失则自动创建）
    /// 成功返回 true（重复调用以最后一次为准）
    static bool install(const std::filesystem::path& dump_dir);

    /// 冒烟钩子：主动触发空指针解引用验证 dump 生成（仅测试用）
    static void crash_for_test();
};

}  // namespace sc::localdesk::platform
