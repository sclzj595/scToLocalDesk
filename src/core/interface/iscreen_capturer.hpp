// ============================================================
// scToLocalDesk | src/core/interface/iscreen_capturer.hpp
// 屏幕采集抽象契约：DXGI / WGC 双实现自动降级（Doc06 §3 / Doc10）
// ============================================================
#pragma once

#include "core/interface/types.hpp"

namespace sc::localdesk {

class IScreenCapturer {
public:
    virtual ~IScreenCapturer() = default;

    /// 初始化采集后端；失败时上层自动降级到下一候选（Doc10 §1）
    virtual bool init() = 0;

    /// 抓取一帧；无新帧返回 false，不阻塞（采集线程专用）
    virtual bool grab(VideoFrame& out) = 0;

    virtual void shutdown() = 0;
};

}  // namespace sc::localdesk
