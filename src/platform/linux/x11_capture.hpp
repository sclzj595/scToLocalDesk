// ============================================================
// scToLocalDesk | src/platform/linux/x11_capture.hpp
// X11 全屏采集（V1.0 豁免解除，Doc18 §8）：XShm 优先 + XGetImage 兜底，
// 仅依赖 libX11（XShm 函数在 libX11 内，无需额外链接）。
// ZPixmap 32bpp 小端 = BGRA 字节序，与 GDI DIB top-down 布局同构，
// 编码器/水印零改动。分辨率取 init 时刻（GDI 同口径）；
// 会话中改分辨率返回 BadMatch → 静默丢帧（静默错误处理器防 abort）。
// 前提：图形会话须为 Xorg（Wayland 下 root 窗口仅含 XWayland 客户端）。
// 仅 Linux（exe 目标编入，不进 core 库）
// ============================================================
#pragma once

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#if __has_include(<X11/extensions/XShm.h>)
    #include <sys/ipc.h>
    #include <sys/shm.h>
    #include <X11/extensions/XShm.h>
    #define SCLD_X11_SHM 1
#endif

#include <chrono>
#include <cstring>
#include <vector>

#include "core/interface/iscreen_capturer.hpp"

namespace sc::localdesk {

class X11Capturer final : public IScreenCapturer {
public:
    ~X11Capturer() override { shutdown(); }

    bool init() override {
        dpy_ = XOpenDisplay(nullptr);
        if (dpy_ == nullptr) {
            return false;
        }
        root_ = DefaultRootWindow(dpy_);
        XWindowAttributes attr{};
        if (XGetWindowAttributes(dpy_, root_, &attr) == 0
            || attr.width == 0 || attr.height == 0) {
            shutdown();
            return false;
        }
        width_ = static_cast<uint32_t>(attr.width);
        height_ = static_cast<uint32_t>(attr.height);
#ifdef SCLD_X11_SHM
        // XShm 快路径：MIT-SHM 扩展可用即常驻共享内存，grab 零拷贝
        if (XShmQueryExtension(dpy_)) {
            shm_img_ = XShmCreateImage(dpy_, attr.visual, attr.depth, ZPixmap,
                                       nullptr, &shminfo_, width_, height_);
            if (shm_img_ != nullptr) {
                const size_t bytes = static_cast<size_t>(shm_img_->bytes_per_line)
                                     * shm_img_->height;
                shminfo_.shmid = shmget(IPC_PRIVATE, bytes, IPC_CREAT | 0600);
                if (shminfo_.shmid >= 0) {
                    // 标准序列：shmaddr 必须同时绑回 image->data（XShmCreateImage
                    // 时 data 传的 nullptr）——漏绑则 XShmGetImage BadValue 静默丢帧
                    shminfo_.shmaddr = static_cast<char*>(
                        shmat(shminfo_.shmid, nullptr, 0));
                    shm_img_->data = shminfo_.shmaddr;
                    shminfo_.readOnly = False;
                    if (shminfo_.shmaddr != reinterpret_cast<void*>(-1)
                        && XShmAttach(dpy_, &shminfo_)) {
                        XSync(dpy_, False);
                        shm_ok_ = true;
                        return true;
                    }
                    if (shminfo_.shmaddr != reinterpret_cast<void*>(-1)) {
                        shmdt(shminfo_.shmaddr);
                    }
                    shmctl(shminfo_.shmid, IPC_RMID, nullptr);
                }
                XDestroyImage(shm_img_);
                shm_img_ = nullptr;
            }
        }
#endif
        return true;  // 无 XShm：grab 走 XGetImage 慢路径（每帧拷贝）
    }

    bool grab(VideoFrame& out) override {
        if (dpy_ == nullptr) {
            return false;
        }
        // BadMatch 静默化：错误处理器压栈 + XSync 确保本帧错误同步到达，
        // 处理完恢复原处理器（Qt 自设的错误处理不受长期劫持）
        const XErrorHandler prev = XSetErrorHandler(QuietErrorHandler);
        XImage* img = nullptr;
#ifdef SCLD_X11_SHM
        if (shm_ok_) {
            // 注意：XShmGetImage 无 format 参数（格式在 Create 时定死），
            // 末参是 plane_mask——传 ZPixmap(=2) 会 BadValue 静默丢帧
            img = XShmGetImage(dpy_, root_, shm_img_, 0, 0, AllPlanes) ? shm_img_
                                                                       : nullptr;
        }
        else
#endif
        {
            img = XGetImage(dpy_, root_, 0, 0, static_cast<int>(width_),
                            static_cast<int>(height_), AllPlanes, ZPixmap);
        }
        XSync(dpy_, False);
        XSetErrorHandler(prev);
        if (img == nullptr) {
            // 抓取失败（BadMatch 分辨率变更等）→ 驱动异常（Doc19 §4.2）；
            // X11 锁屏不影响根窗采集，DesktopLocked 不适用
            last_err_ = CaptureErr::DriverError;
            return false;
        }
        last_err_ = CaptureErr{};  // 成功清回 None（Xlib None 宏污染，字面量不可用）
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        out.data = reinterpret_cast<const uint8_t*>(img->data);
        out.size = static_cast<size_t>(img->bytes_per_line) * img->height;
        out.width = width_;
        out.height = height_;
        out.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
                               now).count();
#ifdef SCLD_X11_SHM
        if (!shm_ok_)
#endif
        {
            // XGetImage 慢路径：XImage 归自己分配，拷进自持缓冲后立刻销毁
            // （out.data 悬垂风险——capture_loop 在 encode 完成前不会释放引用，
            // 但 encode 消费在 grab 返回后的同一帧内，稳妥起见直拷）
            fallback_.resize(out.size);
            std::memcpy(fallback_.data(), img->data, out.size);
            out.data = fallback_.data();
            XDestroyImage(img);
        }
        return true;
    }

    void shutdown() override {
#ifdef SCLD_X11_SHM
        if (dpy_ != nullptr && shm_ok_) {
            XShmDetach(dpy_, &shminfo_);
            XDestroyImage(shm_img_);
            shm_img_ = nullptr;
            shmdt(shminfo_.shmaddr);
            shmctl(shminfo_.shmid, IPC_RMID, nullptr);
            shm_ok_ = false;
        }
        else if (shm_img_ != nullptr) {
            XDestroyImage(shm_img_);
            shm_img_ = nullptr;
        }
#endif
        if (dpy_ != nullptr) {
            XCloseDisplay(dpy_);
            dpy_ = nullptr;
        }
    }

    CaptureErr last_error() const override { return last_err_; }

private:
    /// BadMatch 等错误吞掉（返回 1 = 已处理，阻断默认 abort）
    static int QuietErrorHandler(Display*, XErrorEvent*) {
        return 1;
    }

    Display* dpy_{nullptr};
    Window root_{0};
    uint32_t width_{0};
    uint32_t height_{0};
    CaptureErr last_err_{CaptureErr{}};  ///< 最近一次 grab 失败分类（0=None）
    std::vector<uint8_t> fallback_;  // XGetImage 慢路径自持缓冲
#ifdef SCLD_X11_SHM
    XShmSegmentInfo shminfo_{};
    XImage* shm_img_{nullptr};
    bool shm_ok_{false};
#endif
};

}  // namespace sc::localdesk

// X11 宏污染清屏（Xlib.h 的 None/Status/Bool 等对象式宏会击穿本头之后
// 所有 core 头的同名枚举值——main.cpp 中本头先于 core 头包含）
#undef Bool
#undef None
#undef Status
#undef CursorShape
#undef Expose
#undef KeyPress
#undef KeyRelease
