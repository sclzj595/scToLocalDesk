// ============================================================
// scToLocalDesk | src/platform/qt_adapter/video_surface.hpp
// QML 远程画面渲染件（Doc18 §4.2）：QQuickPaintedItem + 等比铺满
// 帧源：VideoPipeline::frameReady（queued 信号 → 主线程 update()）
// 性能预案：QQuickPaintedItem 卡顿则升级 QSGSimpleTextureNode（先简后繁）
// ============================================================
#pragma once

#include <QImage>
#include <QQuickPaintedItem>

namespace sc::localdesk::platform {

class VideoPipeline;

class VideoSurface : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QObject* pipeline READ pipeline WRITE set_pipeline NOTIFY pipelineChanged)

public:
    explicit VideoSurface(QQuickPaintedItem* parent = nullptr);

    QObject* pipeline() const { return pipeline_; }
    void set_pipeline(QObject* p);

    void paint(QPainter* painter) override;

signals:
    void pipelineChanged();

private:
    QObject* pipeline_{nullptr};  // VideoPipeline*（qobject_cast 检查后使用）
    QImage frame_;
};

}  // namespace sc::localdesk::platform
