// ============================================================
// scToLocalDesk | src/platform/qt_adapter/qt_tray.hpp
// 系统托盘（Doc02 §5.4 / Doc13 §2）：显示窗口 / 停止服务 / 退出
// 无人值守开启：托盘加徽标（Doc16 §7）
// ============================================================
#pragma once

#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>

namespace sc::localdesk::platform {

class TrayController final : public QObject {
    Q_OBJECT

public:
    explicit TrayController(QObject* parent = nullptr);

    void show();
    void set_unattended_badge(bool on);  ///< 无人值守徽标（Doc16 §7）

signals:
    void showMainRequested();
    void stopServiceRequested();
    void quitRequested();

private:
    QSystemTrayIcon tray_;
    QMenu menu_;
    QAction* show_action_{nullptr};
    QAction* stop_action_{nullptr};
    QAction* quit_action_{nullptr};
};

}  // namespace sc::localdesk::platform
