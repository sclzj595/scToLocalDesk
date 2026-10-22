// ============================================================
// scToLocalDesk | src/ui/qwidget/glass_panel.cpp
// ============================================================
#include "glass_panel.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "glass_theme.h"

namespace sc::localdesk::ui {

GlassPanel::GlassPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("glassPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
}

// ---------------- FramelessWindow ----------------

FramelessWindow::FramelessWindow(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
}

void FramelessWindow::beginWindowDrag(const QPoint& globalPos) {
    dragging_ = true;
    drag_offset_ = globalPos - frameGeometry().topLeft();
}

void FramelessWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // 20px 圆角底（Doc13 §1.2）+ 1px 半透描边（红线5）
    QPainterPath path;
    path.addRoundedRect(rect(), GlassTheme::kWinRadius, GlassTheme::kWinRadius);
    p.fillPath(path, GlassTheme::instance().winBg());
    p.setPen(QPen(GlassTheme::instance().glassBorder(), 1));
    p.drawPath(path);
}

void FramelessWindow::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        dragging_ = true;
        drag_offset_ = e->globalPosition().toPoint() - frameGeometry().topLeft();
    }
    QWidget::mousePressEvent(e);
}

void FramelessWindow::mouseMoveEvent(QMouseEvent* e) {
    if (dragging_ && (e->buttons() & Qt::LeftButton)) {
        move(e->globalPosition().toPoint() - drag_offset_);
    }
    QWidget::mouseMoveEvent(e);
}

void FramelessWindow::mouseReleaseEvent(QMouseEvent* e) {
    dragging_ = false;
    QWidget::mouseReleaseEvent(e);
}

void FramelessWindow::changeEvent(QEvent* e) {
    // 主题切换或状态变化时重绘圆角底
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::StyleChange) {
        update();
    }
    QWidget::changeEvent(e);
}

}  // namespace sc::localdesk::ui
