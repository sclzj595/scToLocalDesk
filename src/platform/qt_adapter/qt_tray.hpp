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
    /// 会话知情权（PRD P4）：会话中托盘 tooltip 变更 + 状态/断开菜单项可见
    void set_session_active(bool on, const QString& peer_alias);

signals:
    void showMainRequested();
    void stopServiceRequested();
    void quitRequested();
    void disconnectRequested();  ///< 一键断开当前会话

private:
    QSystemTrayIcon tray_;
    QMenu menu_;
    QAction* show_action_{nullptr};
    QAction* session_action_{nullptr};    ///< "远程协助进行中：X"（禁用状态项）
    QAction* disconnect_action_{nullptr}; ///< 一键断开（会话中可见）
    QAction* stop_action_{nullptr};
    QAction* quit_action_{nullptr};
    bool unattended_on_{false};  ///< 徽标状态记忆（会话结束后 tooltip 恢复用）
};

}  // namespace sc::localdesk::platform
