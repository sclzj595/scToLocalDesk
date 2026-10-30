// ============================================================
// scToLocalDesk | src/foundation/error_text.hpp
// 内部错误码 → 员工可读文案映射（Doc19 §7 员工视图红线）：
// UI 禁止出现 TLV/WebSocket/handshake/challenge/握手等术语与裸错误码，
// 内部日志保留原始错误，UI 一律取本表文案。
// header-only 纯函数零 Qt——Widgets / QML 桥双端共用
// ============================================================
#pragma once

#include <cstdint>

namespace sc::localdesk::foundation {

/// REJECT reason（协议 RejectReason 数值）→ 员工可读拒绝文案
inline const char* friendly_reject(int reject_reason) {
    switch (reject_reason) {
    case 2:  // SessionBusy（单会话红线占用）
        return "对方正忙，请稍后再试";
    case 1:  // PasswordWrong（无人值守密码校验失败）
        return "密码不正确或已被修改";
    case 4:  // Timeout（确认弹窗 30s 未响应自动拒）
        return "对方未响应，请求已过期";
    case 0:  // ManualDenied（人工拒绝）
        return "对方拒绝了本次协助";
    case 3:  // Other（含无人值守开关关闭等）
    default: // 未知 reason 同样不给内部细节
        return "连接被对方拒绝";
    }
}

/// 网络拒连/超时统一文案（拨号失败、等确认超时等出站失败场景）
inline const char* friendly_connect_fail() {
    return "无法连接对方，请确认网络与连接码";
}

/// 采集失败分类（CaptureErr 数值）→ 占位帧提示文案：
/// 错误码数值不外露，员工只见"画面暂不可用"级别的解释
inline const char* friendly_capture_fail(int capture_err) {
    switch (capture_err) {
    case 1:  // DesktopLocked（对方锁屏）
        return "对方屏幕已锁定，画面暂时不可用";
    case 2:  // DriverError（采集驱动异常）
        return "暂时无法获取对方屏幕画面，请提醒对方检查本程序";
    case 0:  // None（正常）
    default:
        return "对方屏幕画面暂时不可用";
    }
}

}  // namespace sc::localdesk::foundation
