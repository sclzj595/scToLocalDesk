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
            return false;
        }
        if (!BitBlt(mem_dc_, 0, 0, static_cast<int>(width_), static_cast<int>(height_),
                    screen_dc_, 0, 0, SRCCOPY | CAPTUREBLT)) {
            return false;
        }
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

    [[nodiscard]] uint32_t width() const noexcept { return width_; }
    [[nodiscard]] uint32_t height() const noexcept { return height_; }

private:
    HDC screen_dc_{nullptr};
    HDC mem_dc_{nullptr};
    HBITMAP bitmap_{nullptr};
    HBITMAP old_bitmap_{nullptr};
    BITMAPINFO bmi_{};
    uint8_t* bits_{nullptr};
    uint32_t width_{0};
    uint32_t height_{0};
};

}  // namespace sc::localdesk
