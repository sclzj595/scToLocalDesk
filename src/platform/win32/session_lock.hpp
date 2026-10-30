// ============================================================
// scToLocalDesk | src/platform/win32/session_lock.hpp
// 本机会话锁屏探测（Doc19 §1.3，P0 锁屏自动拒）：Windows 实现
// ============================================================
#pragma once

namespace sc::localdesk::platform {

/// 本机是否处于锁屏/安全桌面（Winlogon / UAC 提示）。
/// 原理：锁定时 input desktop 切到安全桌面，OpenInputDesktop 拒绝打开；
/// 正常桌面（含未锁屏保）返回有效句柄。探测异常按未锁处理
///（fail-open，不因误判锁死远程可用性；UAC 提示期保守视为锁定）
bool is_session_locked();

}  // namespace sc::localdesk::platform
