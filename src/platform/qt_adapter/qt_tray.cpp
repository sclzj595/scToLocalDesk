// ============================================================
// scToLocalDesk | src/platform/qt_adapter/qt_tray.cpp
// 托盘实现：菜单三动作 + 消息气泡（无人值守接入提示，Doc13 §3.2）
// ============================================================
#include "platform/qt_adapter/qt_tray.hpp"

#include <QAction>
#include <QApplication>
#include <QIcon>

namespace sc::localdesk::platform {

TrayController::TrayController(QObject* parent)
    : QObject(parent) {
    show_action_ = menu_.addAction(QStringLiteral("显示主窗口"));
    menu_.addSeparator();
    stop_action_ = menu_.addAction(QStringLiteral("停止服务"));
    menu_.addSeparator();
    quit_action_ = menu_.addAction(QStringLiteral("退出程序"));

    tray_.setContextMenu(&menu_);
    tray_.setToolTip(QStringLiteral("SC 邻桌"));
    tray_.setIcon(QIcon::fromTheme(QStringLiteral("computer"),
                                   QApplication::windowIcon()));

    connect(show_action_, &QAction::triggered, this,
            &TrayController::showMainRequested);
    connect(stop_action_, &QAction::triggered, this,
            &TrayController::stopServiceRequested);
    connect(quit_action_, &QAction::triggered, this, &TrayController::quitRequested);
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
    tray_.setToolTip(on ? QStringLiteral("SC 邻桌（无人值守开启中）")
                        : QStringLiteral("SC 邻桌"));
}

}  // namespace sc::localdesk::platform
