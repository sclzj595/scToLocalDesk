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
}

void RemoteInputItem::send_mouse(MouseAction action, const QPointF& local, int wheel) {
    const auto mk = InputBridge::map_mouse(static_cast<int>(action), local,
                                           QSizeF(width(), height()),
                                           video_size_, wheel);
    if (mk.has_value()) {
        emit inputMouse(static_cast<int>(mk->action),
                        static_cast<int>(mk->x), static_cast<int>(mk->y),
                        static_cast<int>(mk->wheel));
    }
}

void RemoteInputItem::send_key(int qt_key, bool down) {
    const auto mk = InputBridge::map_key(qt_key, down);
    if (mk.has_value()) {
        emit inputKey(mk->vk, mk->scan, down);
    }
}

void RemoteInputItem::hoverMoveEvent(QHoverEvent* e) {
    send_mouse(MouseAction::Move, e->position());
    e->accept();
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
        send_mouse(MouseAction::MiddleDown, e->position());
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
        send_mouse(MouseAction::MiddleUp, e->position());
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
