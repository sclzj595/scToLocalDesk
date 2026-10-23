// ============================================================
// scToLocalDesk | src/platform/qt_adapter/video_surface.cpp
// ============================================================
#include "platform/qt_adapter/video_surface.hpp"

#include <QPainter>

#include "platform/qt_adapter/video_pipeline.hpp"

namespace sc::localdesk::platform {

VideoSurface::VideoSurface(QQuickPaintedItem* parent)
    : QQuickPaintedItem(parent) {
    // paint 内 drawImage 需抗锯齿关闭（全帧 blit 开 AA 纯浪费）
    setAntialiasing(false);
    setPerformanceHint(QQuickPaintedItem::FastFBOResizing, true);
}

void VideoSurface::set_pipeline(QObject* p) {
    if (pipeline_ == p) {
        return;
    }
    if (pipeline_ != nullptr) {
        disconnect(pipeline_, nullptr, this, nullptr);
    }
    pipeline_ = p;
    if (auto* vp = qobject_cast<VideoPipeline*>(p)) {
        // queued 跨线程投递：解码线程 emit，本件在主线程接收
        connect(vp, &VideoPipeline::frameReady, this,
                [this](const QImage& img) {
                    frame_ = img;
                    update();
                },
                Qt::AutoConnection);
    }
    emit pipelineChanged();
}

void VideoSurface::paint(QPainter* painter) {
    painter->fillRect(boundingRect(), QColor(0, 0, 0));
    if (frame_.isNull()) {
        return;
    }
    // 等比缩放铺满（黑边留白，对齐主流远程桌面观感）
    const QSizeF area = boundingRect().size();
    const QSizeF src = frame_.size();
    const double scale = qMin(area.width() / src.width(),
                              area.height() / src.height());
    const double dw = src.width() * scale;
    const double dh = src.height() * scale;
    painter->drawImage(QRectF((area.width() - dw) / 2, (area.height() - dh) / 2, dw, dh),
                       frame_);
}

}  // namespace sc::localdesk::platform
