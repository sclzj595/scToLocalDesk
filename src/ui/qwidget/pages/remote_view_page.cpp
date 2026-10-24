// scToLocalDesk | 远程画面页（RemoteViewPage）实现（自 pages.cpp 原样拆出）
#include "pages.h"

#include <QColor>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "core/interface/iinput_injector.hpp"
#include "platform/qt_adapter/input_bridge.hpp"
#include "service/remote_session_service.hpp"

namespace sc::localdesk::ui {

// ---------------- 远程画面页 ----------------

RemoteViewPage::RemoteViewPage(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);          // 未按下移动也跟踪（远程光标连续移动）
    setFocusPolicy(Qt::StrongFocus); // 键盘事件前提（点画面 setFocus 接管）
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    toolbar_ = new QWidget(this);
    toolbar_->setObjectName(QStringLiteral("glassPanel"));
    toolbar_->setFixedSize(240, 44);
    auto* toolLay = new QHBoxLayout(toolbar_);
    toolLay->setContentsMargins(16, 6, 16, 6);
    auto* fullBtn = new QPushButton(QStringLiteral("⛶ 全屏"));
    fullBtn->setObjectName(QStringLiteral("secondaryBtn"));
    auto* discBtn = new QPushButton(QStringLiteral("断开连接"));
    discBtn->setObjectName(QStringLiteral("dangerBtn"));
    toolLay->addWidget(fullBtn);
    toolLay->addWidget(discBtn);
    connect(discBtn, &QPushButton::clicked, this, &RemoteViewPage::disconnected);
    toolbar_->hide();  // 无会话默认隐藏（断开按钮不可用），on_frame 恢复

    lay->addStretch();
    auto* toolRow = new QHBoxLayout();
    toolRow->addStretch();
    toolRow->addWidget(toolbar_);
    toolRow->addStretch();
    lay->addLayout(toolRow);
    lay->addSpacing(20);
}

void RemoteViewPage::on_frame(const QImage& img) {
    frame_ = img;
    toolbar_->setVisible(true);  // 有帧 = 会话中，恢复工具条
    update();  // 触发 paintEvent 重画
}

void RemoteViewPage::on_session_closed() {
    frame_ = QImage();
    toolbar_->hide();
    update();
}

void RemoteViewPage::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0));
    if (frame_.isNull()) {
        p.setPen(QColor(0x66, 0x66, 0x66));
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("暂未进行远程控制\n从设备列表或连接码页发起会话后，画面将显示在此处"));
        return;
    }
    // 等比缩放铺满（与 QML VideoSurface 同观感）
    const QSizeF area = size();
    const QSizeF src = frame_.size();
    const double scale = qMin(area.width() / src.width(),
                              area.height() / src.height());
    const double dw = src.width() * scale;
    const double dh = src.height() * scale;
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRectF((area.width() - dw) / 2, (area.height() - dh) / 2, dw, dh),
                frame_);
}

// ---------------- 键鼠透传（Doc18 §4.4，归一化复用 InputBridge） ----------------

void RemoteViewPage::send_mouse(MouseAction action, const QPointF& local, int wheel) {
    if (sessions_ == nullptr) {
        return;
    }
    const auto mk = platform::InputBridge::map_mouse(
        static_cast<int>(action), local, QSizeF(size()), QSizeF(frame_.size()),
        wheel);
    if (mk.has_value()) {
        (void)sessions_->send_input_event(*mk);
    }
}

void RemoteViewPage::mousePressEvent(QMouseEvent* e) {
    setFocus();  // 点画面即接管键盘
    switch (e->button()) {
    case Qt::LeftButton:
        send_mouse(MouseAction::LeftDown, e->position());
        break;
    case Qt::RightButton:
        send_mouse(MouseAction::RightDown, e->position());
        break;
    case Qt::MiddleButton:
        send_mouse(MouseAction::MiddleDown, e->position());
        break;
    default:
        break;
    }
}

void RemoteViewPage::mouseReleaseEvent(QMouseEvent* e) {
    switch (e->button()) {
    case Qt::LeftButton:
        send_mouse(MouseAction::LeftUp, e->position());
        break;
    case Qt::RightButton:
        send_mouse(MouseAction::RightUp, e->position());
        break;
    case Qt::MiddleButton:
        send_mouse(MouseAction::MiddleUp, e->position());
        break;
    default:
        break;
    }
}

void RemoteViewPage::mouseMoveEvent(QMouseEvent* e) {
    send_mouse(MouseAction::Move, e->position());
}

void RemoteViewPage::wheelEvent(QWheelEvent* e) {
    if (e->angleDelta().y() != 0) {
        send_mouse(MouseAction::Wheel, e->position(), e->angleDelta().y());
    }
}

void RemoteViewPage::keyPressEvent(QKeyEvent* e) {
    if (sessions_ != nullptr) {
        const auto mk = platform::InputBridge::map_key(e->key(), true);
        if (mk.has_value()) {
            (void)sessions_->send_input_event(*mk);
        }
    }
}

void RemoteViewPage::keyReleaseEvent(QKeyEvent* e) {
    if (sessions_ != nullptr) {
        const auto mk = platform::InputBridge::map_key(e->key(), false);
        if (mk.has_value()) {
            (void)sessions_->send_input_event(*mk);
        }
    }
}

}  // namespace sc::localdesk::ui
