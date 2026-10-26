// ============================================================
// scToLocalDesk | src/platform/qt_adapter/qt_tray.cpp
// 托盘实现：菜单三动作 + 消息气泡（无人值守接入提示，Doc13 §3.2）
// ============================================================
#include "platform/qt_adapter/qt_tray.hpp"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QStyle>

namespace sc::localdesk::platform {

TrayController::TrayController(QObject* parent)
    : QObject(parent) {
    show_action_ = menu_.addAction(QStringLiteral("显示主窗口"));
    menu_.addSeparator();
    // 会话知情权（PRD P4）：状态项只读展示，断开项默认隐藏
    session_action_ = menu_.addAction(QStringLiteral("远程协助进行中"));
    session_action_->setEnabled(false);
    disconnect_action_ = menu_.addAction(QStringLiteral("断开当前会话"));
    disconnect_action_->setVisible(false);
    menu_.addSeparator();
    stop_action_ = menu_.addAction(QStringLiteral("停止服务"));
    menu_.addSeparator();
    quit_action_ = menu_.addAction(QStringLiteral("退出程序"));

    tray_.setContextMenu(&menu_);
    tray_.setToolTip(QStringLiteral("SC 邻桌"));
    // Windows 无 freedesktop 图标主题，fromTheme 恒失败；应用未设 windowIcon
    // 时回退仍为空（"No Icon set" 警告+托盘空白）——SP_ComputerIcon 系统兜底
    const QIcon fallback =
        QApplication::style() != nullptr
            ? QApplication::style()->standardIcon(QStyle::SP_ComputerIcon)
            : QIcon();
    QIcon icon = QIcon::fromTheme(QStringLiteral("computer"), fallback);
    if (icon.isNull()) {
        icon = fallback;
    }
    tray_.setIcon(icon);

    connect(show_action_, &QAction::triggered, this,
            &TrayController::showMainRequested);
    connect(stop_action_, &QAction::triggered, this,
            &TrayController::stopServiceRequested);
    connect(quit_action_, &QAction::triggered, this, &TrayController::quitRequested);
    connect(disconnect_action_, &QAction::triggered, this,
            &TrayController::disconnectRequested);
    connect(&tray_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::DoubleClick
                    || reason == QSystemTrayIcon::Trigger) {
                    emit showMainRequested();
                }
            });
}

void TrayController::show() {
    tray_.show();
}

void TrayController::set_unattended_badge(bool on) {
    // TODO(V1.0): 合成带角标图标（无人值守开启常显，Doc16 §8 风险对策）
    unattended_on_ = on;
    if (on) {
        tray_.setToolTip(QStringLiteral("SC 邻桌（无人值守开启中）"));
    }
    else if (!disconnect_action_->isVisible()) {  // 会话中不改 tooltip
        tray_.setToolTip(QStringLiteral("SC 邻桌"));
    }
}

void TrayController::set_session_active(bool on, const QString& peer_alias) {
    if (on) {
        session_action_->setText(
            QStringLiteral("远程协助进行中：%1").arg(peer_alias));
        session_action_->setVisible(true);
        disconnect_action_->setVisible(true);
        tray_.setToolTip(QStringLiteral("SC 邻桌（远程协助中：%1）").arg(peer_alias));
    }
    else {
        session_action_->setVisible(false);
        disconnect_action_->setVisible(false);
        tray_.setToolTip(unattended_on_ ? QStringLiteral("SC 邻桌（无人值守开启中）")
                                            : QStringLiteral("SC 邻桌"));
    }
}

void TrayController::set_service_stopped(bool on) {
    stop_action_->setText(on ? QStringLiteral("恢复服务") : QStringLiteral("停止服务"));
    if (on) {
        tray_.setToolTip(QStringLiteral("SC 邻桌（服务已停止）"));
    }
    else if (!disconnect_action_->isVisible()) {  // 会话中 tooltip 由会话态管理
        tray_.setToolTip(unattended_on_ ? QStringLiteral("SC 邻桌（无人值守开启中）")
                                        : QStringLiteral("SC 邻桌"));
    }
}

void TrayController::notify(const QString& title, const QString& body) {
    tray_.showMessage(title, body, QSystemTrayIcon::Information, 5000);
}

}  // namespace sc::localdesk::platform
