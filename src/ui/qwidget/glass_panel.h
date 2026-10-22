// ============================================================
// scToLocalDesk | src/ui/qwidget/glass_panel.h
// 玻璃面板基础件：半透底 + 1px 描边 + 圆角（QSS 模拟磨砂，Doc13 §13.2）
// FramelessWindow：无边框圆角主窗 + 标题栏拖拽（红线4）
// ============================================================
#pragma once

#include <QWidget>

namespace sc::localdesk::ui {

/// 玻璃质感面板：objectName=glassPanel（QSS 统一供样式）
class GlassPanel final : public QWidget {
    Q_OBJECT

public:
    explicit GlassPanel(QWidget* parent = nullptr);
};

/// 无边框圆角主窗：20px 圆角自绘底（Doc13 §1.2）
class FramelessWindow : public QWidget {
    Q_OBJECT

public:
    explicit FramelessWindow(QWidget* parent = nullptr);

    /// 标题栏拖拽起始（由 TitleBar 转发鼠标事件）
    void beginWindowDrag(const QPoint& globalPos);
    /// 窗口按钮区命中判断（边缘 8px 支持拖拽缩放，简化期仅移动）

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    QPoint drag_offset_;
    bool dragging_{false};
};

}  // namespace sc::localdesk::ui
