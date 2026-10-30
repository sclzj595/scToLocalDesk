// ============================================================
// scToLocalDesk | src/platform/qt_adapter/video_surface.hpp
// QML 远程画面渲染件（Doc18 §4.2）：QQuickPaintedItem + 等比铺满
// 帧源：VideoPipeline::frameReady（queued 信号 → 主线程 update()）
// 占位帧（Doc19 §4.2）：placeholder/placeholderText 暴露给 QML 叠层
// 性能预案：QQuickPaintedItem 卡顿则升级 QSGSimpleTextureNode（先简后繁）
// ============================================================
#pragma once

#include <QImage>
#include <QQuickPaintedItem>
#include <QString>

namespace sc::localdesk::platform {

class VideoPipeline;

class VideoSurface : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QObject* pipeline READ pipeline WRITE set_pipeline NOTIFY pipelineChanged)
    /// 采集失败占位（Doc19 §4.2）：占位帧置 true，正常帧到达复位 false
    Q_PROPERTY(bool placeholder READ placeholder NOTIFY placeholderChanged)
    /// 占位提示文案：锁屏 / 驱动异常（含错误码）两条，与 Widgets 版同口径
    Q_PROPERTY(QString placeholderText READ placeholderText NOTIFY placeholderTextChanged)

public:
    explicit VideoSurface(QQuickPaintedItem* parent = nullptr);

    QObject* pipeline() const { return pipeline_; }
    void set_pipeline(QObject* p);

    bool placeholder() const { return placeholder_; }
    QString placeholderText() const { return placeholder_text_; }

    void paint(QPainter* painter) override;

signals:
    void pipelineChanged();
    void placeholderChanged();
    void placeholderTextChanged();

private:
    QObject* pipeline_{nullptr};  // VideoPipeline*（qobject_cast 检查后使用）
    QImage frame_;
    bool placeholder_{false};
    QString placeholder_text_;
};

}  // namespace sc::localdesk::platform
