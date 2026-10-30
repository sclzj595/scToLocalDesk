// ============================================================
// scToLocalDesk | src/implement/capture/gdi_capture.hpp
// GDI BitBlt 屏幕采集：骨架期默认实现（全 Windows 可用，无系统版本门槛）
// DXGI/WGC 高性能实现为 V1.0 任务（Doc10 §1），本实现保证管线端到端可跑
// 仅 Windows（exe 目标编入，不进 core 库）
// ============================================================
#pragma once

#include <windows.h>

#include <vector>

#include "core/interface/iscreen_capturer.hpp"

namespace sc::localdesk {

class GdiCapturer final : public IScreenCapturer {
public:
    ~GdiCapturer() override { shutdown(); }

    bool init() override {
        screen_dc_ = GetDC(nullptr);
        if (screen_dc_ == nullptr) {
            return false;
        }
        width_ = static_cast<uint32_t>(GetDeviceCaps(screen_dc_, HORZRES));
        height_ = static_cast<uint32_t>(GetDeviceCaps(screen_dc_, VERTRES));
        if (width_ == 0 || height_ == 0) {
            shutdown();
            return false;
        }
        mem_dc_ = CreateCompatibleDC(screen_dc_);

        bmi_.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi_.bmiHeader.biWidth = static_cast<LONG>(width_);
        bmi_.bmiHeader.biHeight = -static_cast<LONG>(height_);  // top-down
        bmi_.bmiHeader.biPlanes = 1;
        bmi_.bmiHeader.biBitCount = 32;  // BGRA
        bmi_.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        bitmap_ = CreateDIBSection(mem_dc_, &bmi_, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bitmap_ == nullptr || bits == nullptr) {
            shutdown();
            return false;
        }
        bits_ = static_cast<uint8_t*>(bits);
        old_bitmap_ = static_cast<HBITMAP>(SelectObject(mem_dc_, bitmap_));
        return true;
    }

    bool grab(VideoFrame& out) override {
        if (screen_dc_ == nullptr || mem_dc_ == nullptr) {
            return false;  // 未初始化：不计错误（上层以 init 失败终止推流）
        }
        if (!BitBlt(mem_dc_, 0, 0, static_cast<int>(width_), static_cast<int>(height_),
                    screen_dc_, 0, 0, SRCCOPY | CAPTUREBLT)) {
            // 失败分类（Doc19 §4.2）：输入桌面打不开=锁屏/安全桌面切换
            // （BitBlt 对锁屏桌面必败）；桌面可达仍失败=采集驱动异常
            const HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
            if (input != nullptr) {
                CloseDesktop(input);
                last_err_ = CaptureErr::DriverError;
            }
            else {
                last_err_ = CaptureErr::DesktopLocked;
            }
            return false;
        }
        compose_cursor();  // 光标合成（Doc19 §4.4）：失败静默画无光标帧
        last_err_ = CaptureErr::None;
        LARGE_INTEGER ts{};
        QueryPerformanceCounter(&ts);
        out.data = bits_;
        out.size = static_cast<size_t>(width_) * height_ * 4;
        out.width = width_;
        out.height = height_;
        out.timestamp_us = ts.QuadPart;
        return true;
    }

    void shutdown() override {
        if (mem_dc_ != nullptr && old_bitmap_ != nullptr) {
            SelectObject(mem_dc_, old_bitmap_);
        }
        if (bitmap_ != nullptr) {
            DeleteObject(bitmap_);
            bitmap_ = nullptr;
        }
        if (mem_dc_ != nullptr) {
            DeleteDC(mem_dc_);
            mem_dc_ = nullptr;
        }
        if (screen_dc_ != nullptr) {
            ReleaseDC(nullptr, screen_dc_);
            screen_dc_ = nullptr;
        }
    }

    uint32_t width() const noexcept { return width_; }
    uint32_t height() const noexcept { return height_; }

    CaptureErr last_error() const override { return last_err_; }

private:
    /// 光标合成（Doc19 §4.4）：BitBlt 采到的画面不含鼠标光标——控制端看不到
    /// 对端鼠标位置（远程协助核心痛点）。GetCursorInfo 取当前光标句柄+屏幕
    /// 坐标，DrawIconEx 原尺寸画进同一内存 DC（即帧 BGRA 缓冲，ARGB 光标
    /// 自动 alpha 混合）；隐藏光标/失败/安全桌面静默跳过，不影响主帧率。
    /// 全屏采集（0,0 起）与屏幕坐标同系，1:1 直画不缩放
    void compose_cursor() {
        CURSORINFO ci{};
        ci.cbSize = sizeof(ci);
        if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING)
            || ci.hCursor == nullptr) {
            return;
        }
        (void)DrawIconEx(mem_dc_, ci.ptScreenPos.x, ci.ptScreenPos.y,
                         ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
    }

    HDC screen_dc_{nullptr};
    HDC mem_dc_{nullptr};
    HBITMAP bitmap_{nullptr};
    HBITMAP old_bitmap_{nullptr};
    BITMAPINFO bmi_{};
    uint8_t* bits_{nullptr};
    uint32_t width_{0};
    uint32_t height_{0};
    CaptureErr last_err_{CaptureErr::None};  ///< 最近一次 grab 失败分类
};

}  // namespace sc::localdesk
