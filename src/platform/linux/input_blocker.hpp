// ============================================================
// scToLocalDesk | src/platform/linux/input_blocker.hpp
// 会话内一键冻结键鼠输入（Doc19 §2.1）：Linux X11 抓取实现
// ============================================================
#pragma once

#include <functional>

namespace sc::localdesk::platform {

/// 冻结/恢复本机键鼠输入（双向锁：物理+注入事件全被 X server 抓给本进程
/// 丢弃，画面继续传）。实现：XGrabKeyboard + XGrabPointer（Async 双向，
/// 事件不读即丢）。X11 抓取绑定显示连接——进程退出连接断开自动解冻
///（崩溃安全）。逃生通道：会话断开自动解除 / 30 分钟上限自动解除 /
/// Ctrl+Alt+F3 等 VT 切换（内核级，X 抓取不影响）。
/// 返回 false = 抓取失败（被屏幕保护/其他客户端抢占，调用方回弹勾选）
bool set_input_frozen(bool on);

/// 当前冻结态
bool is_input_frozen();

/// 冻结状态变化回执（Linux 无钩子内自解路径，预留接口对齐 Windows 版）
void set_freeze_changed_callback(std::function<void(bool)> cb);

}  // namespace sc::localdesk::platform
