// ============================================================
// scToLocalDesk | src/platform/qt_adapter/input_bridge.hpp
// 键鼠透传共用映射（Doc18 §4.4，纯函数无状态）：
//   UI 局部事件 → 0~65535 归一化 MouseKeyBody
// QML（RemoteInputItem）与 Widgets（RemoteViewPage）双端复用；
// 坐标反算与渲染端等比 letterbox（VideoSurface/RemoteViewPage paint）同式
// ============================================================
#pragma once

#include <cstdint>
#include <utility>

#include <QPointF>
#include <QSizeF>

#include "core/interface/iinput_injector.hpp"  // MouseAction
#include "core/protocol/tlv.hpp"               // MouseKeyBody

namespace sc::localdesk::platform {

struct InputBridge final {
    /// 局部坐标 → 归一化（黑边区 clamp 进画面，主流远程桌面行为）。
    /// action 为 MouseAction 底层值；wheel 仅 Wheel 动作使用（120 = 一格）。
    /// 无帧（video 空）或显示区为空返回 false，调用方不发帧。
    static bool map_mouse(int action, const QPointF& local, const QSizeF& view,
                          const QSizeF& video, protocol::MouseKeyBody& mk,
                          int wheel = 0) {
        if (video.isEmpty() || view.isEmpty()) {
            return false;
        }
        mk.kind = protocol::MouseKeyKind::Mouse;
        mk.action = static_cast<uint8_t>(action);
        if (action == static_cast<int>(MouseAction::Wheel)) {
            mk.wheel = static_cast<int16_t>(wheel);
            return true;
        }
        const double scale = qMin(view.width() / video.width(),
                                  view.height() / video.height());
        const double dw = video.width() * scale;
        const double dh = video.height() * scale;
        const double ox = (view.width() - dw) / 2.0;
        const double oy = (view.height() - dh) / 2.0;
        const double lx = qBound(ox, local.x(), ox + dw);
        const double ly = qBound(oy, local.y(), oy + dh);
        mk.x = static_cast<uint16_t>((lx - ox) / dw * 65535.0 + 0.5);
        mk.y = static_cast<uint16_t>((ly - oy) / dh * 65535.0 + 0.5);
        return true;
    }

    /// Qt::Key → Windows VK（scan 填 0 走 VK 路径注入）。
    /// ASCII 区标点的 Qt 值与 VK 不一致（OEM 码），一律查表；表外丢弃。
    static bool map_key(int qt_key, bool down, protocol::MouseKeyBody& mk) {
        uint16_t vk = 0;
        bool hit = false;
        for (const auto& kv : kKeyTable) {
            if (kv.first == qt_key) {
                vk = kv.second;
                hit = true;
                break;
            }
        }
        if (!hit && qt_key >= 0x01000030 && qt_key <= 0x0100003b) {
            vk = static_cast<uint16_t>(0x70 + (qt_key - 0x01000030));  // F1-F12 顺延
            hit = true;
        }
        if (!hit) {
            // 数字 0-9 / 字母 A-Z / 空格：Qt 与 VK 同值，直接透传
            const bool passthrough = (qt_key >= '0' && qt_key <= '9')
                                  || (qt_key >= 'A' && qt_key <= 'Z')
                                  || qt_key == 0x20;
            if (!passthrough) {
                return false;
            }
            vk = static_cast<uint16_t>(qt_key);
        }
        mk.kind = protocol::MouseKeyKind::Keyboard;
        mk.vk = vk;
        mk.down = down ? 1 : 0;
        return true;
    }

private:
    // Qt::Key → VK 显式映射（OEM 标点 / 功能 / 导航 / 修饰）
    static constexpr std::pair<int, uint16_t> kKeyTable[] = {
        {0x2d, 0xbd},         // - → VK_OEM_MINUS
        {0x3d, 0xbb},         // = → VK_OEM_PLUS
        {0x5b, 0xdb},         // [ → VK_OEM_4
        {0x5d, 0xdd},         // ] → VK_OEM_6
        {0x5c, 0xdc},         // \ → VK_OEM_5
        {0x3b, 0xba},         // ; → VK_OEM_1
        {0x27, 0xde},         // ' → VK_OEM_7
        {0x2c, 0xbc},         // , → VK_OEM_COMMA
        {0x2e, 0xbe},         // . → VK_OEM_PERIOD
        {0x2f, 0xbf},         // / → VK_OEM_2
        {0x60, 0xc0},         // ` → VK_OEM_3
        {0x01000000, 0x1b},   // Esc
        {0x01000001, 0x09},   // Tab
        {0x01000003, 0x08},   // Backspace
        {0x01000004, 0x0d},   // Return
        {0x01000005, 0x0d},   // Enter（小键盘）
        {0x01000006, 0x2d},   // Insert
        {0x01000007, 0x2e},   // Delete
        {0x01000010, 0x24},   // Home
        {0x01000011, 0x23},   // End
        {0x01000012, 0x25},   // Left
        {0x01000013, 0x26},   // Up
        {0x01000014, 0x27},   // Right
        {0x01000015, 0x28},   // Down
        {0x01000016, 0x21},   // PageUp
        {0x01000017, 0x22},   // PageDown
        {0x01000020, 0x10},   // Shift
        {0x01000021, 0x11},   // Control
        {0x01000022, 0x5b},   // Meta → VK_LWIN
        {0x01000023, 0x12},   // Alt
        {0x01000024, 0x14},   // CapsLock
        {0x01000030, 0x70},   // F1（F2-F12 顺延：0x01000030+i → 0x70+i）
    };
};

}  // namespace sc::localdesk::platform
