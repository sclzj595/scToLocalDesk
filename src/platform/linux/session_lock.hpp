// ============================================================
// scToLocalDesk | src/platform/linux/session_lock.hpp
// 本机会话锁屏探测（Doc19 §1.3，P0 锁屏自动拒）：Linux X11 实现
// ============================================================
#pragma once

namespace sc::localdesk::platform {

/// 本机是否处于锁屏状态（GNOME 席位锁等）。
/// 经 D-Bus org.freedesktop.ScreenSaver.GetActive 查询；探测失败
///（工具缺失/无会话总线/非桌面环境）按未锁处理——fail-open，
/// 不因探测失效锁死远程可用性
bool is_session_locked();

}  // namespace sc::localdesk::platform
