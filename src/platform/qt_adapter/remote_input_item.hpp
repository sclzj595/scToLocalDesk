// ============================================================
// scToLocalDesk | src/platform/qt_adapter/remote_input_item.hpp
// QML 键鼠捕获件（Doc18 §4.4）：盖在远程画面上收鼠标/滚轮/键盘事件，
// 经 InputBridge 归一化后发 inputMouse/inputKey 信号（QML 连线 bridge 发送）
// 职责单一：只捕获与归一化，不接触会话/传输
// ============================================================
#pragma once

#include <QElapsedTimer>
#include <QPoint>
#include <QPointF>
#include <QQuickItem>
#include <QTimer>

#include "platform/qt_adapter/input_bridge.hpp"

namespace sc::localdesk::platform {

class RemoteInputItem : public QQuickItem {  // 不标 final：QQmlElement 需继承（Qt6 已知坑）
    Q_OBJECT
    /// 视频分辨率（QML 绑 VideoPipeline.frameSize）；空尺寸时不发事件
    Q_PROPERTY(QSizeF videoSize READ videoSize WRITE setVideoSize
                   NOTIFY videoSizeChanged)
    /// 画面缩放模式（Doc19 §4.4）：0 自适应 1 拉伸 2 1:1（中键拖动平移）
    Q_PROPERTY(int scaleMode READ scaleMode WRITE setScaleMode
                   NOTIFY scaleModeChanged)
    /// Pixel 平移偏移（本件为唯一真源：拖动更新，VideoSurface 绑定绘制）
    Q_PROPERTY(QPointF panOffset READ panOffset NOTIFY panOffsetChanged)

public:
    explicit RemoteInputItem(QQuickItem* parent = nullptr);

    QSizeF videoSize() const { return video_size_; }
    void setVideoSize(QSizeF s) {
        if (video_size_ != s) {
            video_size_ = s;
            clamp_pan();  // Pixel 平移随分辨率变化收敛（防出界）
            emit videoSizeChanged();
        }
    }
    int scaleMode() const { return scale_mode_; }
    void setScaleMode(int m);
    QPointF panOffset() const { return pan_; }

signals:
    void videoSizeChanged();
    void scaleModeChanged();
    void panOffsetChanged();
    /// 已归一化（0~65535）/滚轮增量，QML 连线 ScLocaldesk.sendMouse
    void inputMouse(int action, int x, int y, int wheel);
    /// Windows VK 注入参数，QML 连线 ScLocaldesk.sendKey
    void inputKey(int vk, int scan, bool down);

protected:
    void hoverMoveEvent(QHoverEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;

private:
    void send_mouse(MouseAction action, const QPointF& local, int wheel = 0);
    void send_key(int qt_key, bool down);
    /// Pixel 平移偏移收敛：视口内恒有画面（画面小于视口收敛为 0）
    void clamp_pan();

    QSizeF video_size_;
    int scale_mode_{0};  // 0 Fit | 1 Stretch | 2 Pixel（Doc19 §4.4）
    QPointF pan_;        // Pixel 平移偏移（画面左上角相对视口负偏移）
    bool mid_pressed_{false};  ///< Pixel 中键按压中（Down 延迟至 release 判定）
    bool mid_moved_{false};    ///< 本次按压已超阈值进入拖动（不再补发中键点击）
    QPointF mid_last_;         ///< 平移增量起点（拖动方向计算）

    // 移动节流（远控手感优化）：hover 移动事件可 500+/s，直接转发会灌爆
    // 对端 X socket——按键事件排在移动洪流后 = 打字发滞。合并为最新坐标、
    // 10ms 定时器统一发送（≤100Hz），点击/按键不受节流影响
    QTimer move_timer_;
    QPointF pending_move_;
    bool move_pending_{false};
};

}  // namespace sc::localdesk::platform
