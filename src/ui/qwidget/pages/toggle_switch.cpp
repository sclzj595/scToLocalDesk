// ============================================================
// scToLocalDesk | src/ui/qwidget/pages/toggle_switch.cpp
// ToggleSwitch 自绘滑动开关实现（自 pages.cpp 原样拆出）
// ============================================================
#include "pages.h"

#include <QColor>
#include <QEvent>
#include <QPainter>
#include <QPropertyAnimation>

#include "../glass_theme.h"

namespace sc::localdesk::ui {

// ---------------- 滑动开关（对齐 QML SwitchRow） ----------------

ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(42, 24);
    auto* anim = new QPropertyAnimation(this, "knob", this);
    anim->setDuration(120);   // Doc13 §6：120ms
    connect(this, &QAbstractButton::toggled, this, [this, anim](bool on) {
        anim->stop();
        anim->setStartValue(knob_);
        anim->setEndValue(on ? 1.0 : 0.0);
        anim->start();
    });
}

void ToggleSwitch::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // 轨道：选中主色 / 未选分割线色，hover 微亮
    auto track = isChecked() ? GlassTheme::instance().primary()
                             : GlassTheme::instance().divider();
    if (hover_) {
        track = track.lighter(110);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(track);
    p.drawRoundedRect(rect(), 12, 12);
    // 圆钮：白色，位置随 knob 动画
    const qreal x = 3.0 + knob_ * (width() - 24.0);
    p.setBrush(QColor(Qt::white));
    p.drawEllipse(QRectF(x, 3.0, 18.0, 18.0));
}

void ToggleSwitch::enterEvent(QEnterEvent* e) {
    hover_ = true;
    update();
    QAbstractButton::enterEvent(e);
}

void ToggleSwitch::leaveEvent(QEvent* e) {
    hover_ = false;
    update();
    QAbstractButton::leaveEvent(e);
}

}  // namespace sc::localdesk::ui
