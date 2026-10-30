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
    /// 停止服务 toggle（Doc02 §5.4）：菜单文案 停止服务↔恢复服务 + tooltip
    void set_service_stopped(bool on);
    /// 托盘气泡（无人值守接入提示等，Doc16 §2.5 可见性）
    void notify(const QString& title, const QString& body);
    /// 键鼠冻结勾选态回执（实际执行结果/自动解除后同步；blockSignals 防回环）
    void set_input_frozen_state(bool on);

signals:
    void showMainRequested();
    void stopServiceRequested();
    void quitRequested();
    void disconnectRequested();  ///< 一键断开所有远程会话（红标项）
    void freezeInputRequested(bool frozen);  ///< 冻结键鼠输入勾选切换（Doc19 §2.1）

private:
    QSystemTrayIcon tray_;
    QMenu menu_;
    QAction* show_action_{nullptr};
    QAction* session_action_{nullptr};    ///< "远程协助进行中：X"（禁用状态项）
    QAction* disconnect_action_{nullptr}; ///< 一键断开（红标；无会话禁用）
    QAction* freeze_action_{nullptr};     ///< 冻结键鼠输入（勾选；会话中可用）
    QAction* stop_action_{nullptr};
    QAction* quit_action_{nullptr};
    bool unattended_on_{false};  ///< 徽标状态记忆（会话结束后 tooltip 恢复用）
    QIcon base_icon_;            ///< 无徽标基础图标（徽标合成/恢复用）
    QIcon red_dot_icon_;         ///< 断开项红色标记图标（构造期合成一次）
};

}  // namespace sc::localdesk::platform
