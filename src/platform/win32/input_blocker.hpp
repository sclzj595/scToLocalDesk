// ============================================================
// scToLocalDesk | src/platform/win32/input_blocker.hpp
// 会话内一键冻结键鼠输入（Doc19 §2.1）：Windows 低级钩子实现
// ============================================================
#pragma once

#include <functional>

namespace sc::localdesk::platform {

/// 冻结/恢复本机键鼠输入（双向锁：物理+注入事件全丢，画面继续传）。
/// 实现：WH_KEYBOARD_LL/WH_MOUSE_LL 低级钩子逐事件丢弃（须在含消息循环的
/// 线程调用——托盘所在 Qt 主线程满足）。逃生通道三重保障：
///   1. 自家进程窗口（托盘弹出菜单/主窗）事件放行——托盘取消勾选即可解除；
///   2. 冻结期按 Ctrl+Alt+Shift+F12 钩子内直接解除（回调回执 false）；
///   3. Ctrl+Alt+Del 系统级不受低级钩子影响（Winlogon 安全桌面前置处理）
/// 返回 false = 钩子注册失败（未生效，调用方回弹菜单勾选态并 Warn）
bool set_input_frozen(bool on);

/// 当前冻结态（与 set_input_frozen/快捷键自解保持一致）
bool is_input_frozen();

/// 冻结状态变化回执（快捷键在钩子内自行解除时回投 false；主线程同步回调）。
/// 未注入则无回执（托盘勾选态靠 is_input_frozen 轮询兜底）
void set_freeze_changed_callback(std::function<void(bool)> cb);

}  // namespace sc::localdesk::platform
