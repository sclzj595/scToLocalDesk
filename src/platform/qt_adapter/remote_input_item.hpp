// ============================================================
// scToLocalDesk | src/platform/qt_adapter/remote_input_item.hpp
// QML 键鼠捕获件（Doc18 §4.4）：盖在远程画面上收鼠标/滚轮/键盘事件，
// 经 InputBridge 归一化后发 inputMouse/inputKey 信号（QML 连线 bridge 发送）
// 职责单一：只捕获与归一化，不接触会话/传输
// ============================================================
#pragma once

#include <QQuickItem>

#include "platform/qt_adapter/input_bridge.hpp"

namespace sc::localdesk::platform {

class RemoteInputItem : public QQuickItem {  // 不标 final：QQmlElement 需继承（Qt6 已知坑）
    Q_OBJECT
    /// 视频分辨率（QML 绑 VideoPipeline.frameSize）；空尺寸时不发事件
    Q_PROPERTY(QSizeF videoSize READ videoSize WRITE setVideoSize
                   NOTIFY videoSizeChanged)

public:
    explicit RemoteInputItem(QQuickItem* parent = nullptr);

    QSizeF videoSize() const { return video_size_; }
    void setVideoSize(QSizeF s) {
        if (video_size_ != s) {
            video_size_ = s;
            emit videoSizeChanged();
        }
    }

signals:
    void videoSizeChanged();
    /// 已归一化（0~65535）/滚轮增量，QML 连线 ScLocaldesk.sendMouse
    void inputMouse(int action, int x, int y, int wheel);
    /// Windows VK 注入参数，QML 连线 ScLocaldesk.sendKey
    void inputKey(int vk, int scan, bool down);

protected:
    void hoverMoveEvent(QHoverEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;

private:
    void send_mouse(MouseAction action, const QPointF& local, int wheel = 0);
    void send_key(int qt_key, bool down);

    QSizeF video_size_;
};

}  // namespace sc::localdesk::platform
