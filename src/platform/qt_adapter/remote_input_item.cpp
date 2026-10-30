// ============================================================
// scToLocalDesk | src/platform/qt_adapter/remote_input_item.cpp
// ============================================================
#include "platform/qt_adapter/remote_input_item.hpp"

#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

namespace sc::localdesk::platform {

RemoteInputItem::RemoteInputItem(QQuickItem* parent) : QQuickItem(parent) {
    setAcceptHoverEvents(true);  // 未按下移动也跟踪（远程光标连续移动）
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    setAcceptTouchEvents(false);
    setKeepMouseGrab(true);
    move_timer_.setSingleShot(true);
    move_timer_.setInterval(10);
    QObject::connect(&move_timer_, &QTimer::timeout, this, [this] {
        if (move_pending_) {
            move_pending_ = false;
            send_mouse(MouseAction::Move, pending_move_);
        }
    });
}

void RemoteInputItem::send_mouse(MouseAction action, const QPointF& local, int wheel) {
    protocol::MouseKeyBody mk;
    // Pixel 模式无 letterbox：帧像素 = 局部坐标 + 平移偏移（Doc19 §4.4）
    const bool ok = scale_mode_ == 2
        ? InputBridge::map_mouse_pixel(static_cast<int>(action), local, pan_,
                                       video_size_, mk, wheel)
        : InputBridge::map_mouse(static_cast<int>(action), local,
                                 QSizeF(width(), height()), video_size_, mk,
                                 wheel);
    if (ok) {
        emit inputMouse(static_cast<int>(mk.action),
                        static_cast<int>(mk.x), static_cast<int>(mk.y),
                        static_cast<int>(mk.wheel));
    }
}

void RemoteInputItem::send_key(int qt_key, bool down) {
    protocol::MouseKeyBody mk;
    if (InputBridge::map_key(qt_key, down, mk)) {
        emit inputKey(mk.vk, mk.scan, down);
    }
}

void RemoteInputItem::hoverMoveEvent(QHoverEvent* e) {
    // 节流合并：只记最新坐标，10ms 定时器统一发（中间坐标无意义，丢弃）
    pending_move_ = e->position();
    if (!move_pending_) {
        move_pending_ = true;
        move_timer_.start();
    }
    e->accept();
}

void RemoteInputItem::mouseMoveEvent(QMouseEvent* e) {
    if (scale_mode_ == 2 && mid_pressed_
        && (e->buttons() & Qt::MiddleButton) != 0) {
        if (!mid_moved_ && (e->position() - mid_last_).manhattanLength() > 8) {
            mid_moved_ = true;  // 超阈值进入拖动：松开不再补发中键点击
        }
        if (mid_moved_) {
            pan_ += mid_last_ - e->position();  // 拖动方向与画面一致
            clamp_pan();
            mid_last_ = e->position();
            emit panOffsetChanged();
            e->accept();
            return;  // 平移中不向远端发移动（本地视口操作）
        }
    }
    send_mouse(MouseAction::Move, e->position());
    e->accept();
}

void RemoteInputItem::setScaleMode(int m) {
    m = qBound(0, m, 2);
    if (scale_mode_ == m) {
        return;
    }
    scale_mode_ = m;
    if (m == 2 && !video_size_.isEmpty()) {
        // 进入 1:1：居中定位（画面小于视口 clamp 收敛为 0）
        pan_ = QPointF(qMax(0.0, (video_size_.width() - width()) / 2.0),
                       qMax(0.0, (video_size_.height() - height()) / 2.0));
    }
    else {
        pan_ = QPointF(0, 0);  // 回 Fit/Stretch：无平移
    }
    emit scaleModeChanged();
    emit panOffsetChanged();
}

void RemoteInputItem::clamp_pan() {
    if (video_size_.isEmpty()) {
        pan_ = QPointF(0, 0);
        return;
    }
    pan_ = QPointF(qBound(0.0, pan_.x(), qMax(0.0, video_size_.width() - width())),
                   qBound(0.0, pan_.y(),
                          qMax(0.0, video_size_.height() - height())));
}

void RemoteInputItem::mousePressEvent(QMouseEvent* e) {
    forceActiveFocus();  // 点画面即接管键盘（远端输入焦点跟随）
    switch (e->button()) {
    case Qt::LeftButton:
        send_mouse(MouseAction::LeftDown, e->position());
        break;
    case Qt::RightButton:
        send_mouse(MouseAction::RightDown, e->position());
        break;
    case Qt::MiddleButton:
        if (scale_mode_ == 2) {
            // 1:1 平移（Doc19 §4.4）：按压不立即透传——超阈值拖动=平移，
            // 原地松开=中键点击（release 补发 Down+Up，中键功能不丢）
            mid_pressed_ = true;
            mid_moved_ = false;
            mid_last_ = e->position();
        }
        else {
            send_mouse(MouseAction::MiddleDown, e->position());
        }
        break;
    default:
        break;
    }
    e->accept();
}

void RemoteInputItem::mouseReleaseEvent(QMouseEvent* e) {
    switch (e->button()) {
    case Qt::LeftButton:
        send_mouse(MouseAction::LeftUp, e->position());
        break;
    case Qt::RightButton:
        send_mouse(MouseAction::RightUp, e->position());
        break;
    case Qt::MiddleButton:
        if (scale_mode_ == 2 && mid_pressed_) {
            const bool was_click = !mid_moved_;
            mid_pressed_ = false;
            if (was_click) {  // 未拖动 = 中键点击：补发按下+抬起
                send_mouse(MouseAction::MiddleDown, e->position());
                send_mouse(MouseAction::MiddleUp, e->position());
            }
        }
        else {
            send_mouse(MouseAction::MiddleUp, e->position());
        }
        break;
    default:
        break;
    }
    e->accept();
}

void RemoteInputItem::wheelEvent(QWheelEvent* e) {
    const QPoint delta = e->angleDelta();
    if (delta.y() != 0) {
        send_mouse(MouseAction::Wheel, e->position(), delta.y());
    }
    e->accept();
}

void RemoteInputItem::keyPressEvent(QKeyEvent* e) {
    send_key(e->key(), true);
    e->accept();
}

void RemoteInputItem::keyReleaseEvent(QKeyEvent* e) {
    send_key(e->key(), false);
    e->accept();
}

}  // namespace sc::localdesk::platform
