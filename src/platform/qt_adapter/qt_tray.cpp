// ============================================================
// scToLocalDesk | src/platform/qt_adapter/qt_tray.cpp
// 托盘实现：菜单三动作 + 消息气泡（无人值守接入提示，Doc13 §3.2）
// ============================================================
#include "platform/qt_adapter/qt_tray.hpp"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QStyle>

#include "ui/qwidget/popups/glass_dialog.hpp"

namespace sc::localdesk::platform {

TrayController::TrayController(QObject* parent)
    : QObject(parent) {
    // 托盘菜单与站内菜单同源（GlassDialog 门面）：令牌 QSS + Win11
    // DWM 圆角/描边；主题明暗随 GlassTheme 单例实时取值（QML 端经
    // setUiTheme 桥接同步，两端形态一致）
    sc::localdesk::ui::GlassDialog::styleGlassMenu(&menu_);
    show_action_ = menu_.addAction(QStringLiteral("显示主窗口"));
    menu_.addSeparator();
    // 会话知情权（PRD P4）：状态项只读展示；一键断开红标项恒显（无会话禁用）
    session_action_ = menu_.addAction(QStringLiteral("远程协助进行中"));
    session_action_->setEnabled(false);
    // 红色标记（QMenu 不渲染富文本，画红圆点图标作视觉警示）
    QPixmap red_pm(12, 12);
    red_pm.fill(Qt::transparent);
    QPainter rp(&red_pm);
    rp.setRenderHint(QPainter::Antialiasing);
    rp.setPen(Qt::NoPen);
    rp.setBrush(QColor(0xE5, 0x48, 0x4D));
    rp.drawEllipse(1, 1, 10, 10);
    rp.end();
    red_dot_icon_ = QIcon(red_pm);
    disconnect_action_ = menu_.addAction(red_dot_icon_,
                                         QStringLiteral("断开所有远程会话"));
    disconnect_action_->setEnabled(false);
    // 会话内一键冻结输入（Doc19 §2.1）：双向锁语义——物理+注入全丢，
    // 画面继续传；勾选态由执行结果回执（set_input_frozen_state）
    freeze_action_ = menu_.addAction(QStringLiteral("冻结键鼠输入（画面继续）"));
    freeze_action_->setCheckable(true);
    freeze_action_->setEnabled(false);
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
    base_icon_ = icon;
    tray_.setIcon(icon);

    connect(show_action_, &QAction::triggered, this,
            &TrayController::showMainRequested);
    connect(stop_action_, &QAction::triggered, this,
            &TrayController::stopServiceRequested);
    connect(quit_action_, &QAction::triggered, this, &TrayController::quitRequested);
    connect(disconnect_action_, &QAction::triggered, this,
            &TrayController::disconnectRequested);
    // 勾选切换即上抛（执行成败由 main 回执 set_input_frozen_state 回弹）
    connect(freeze_action_, &QAction::toggled, this,
            [this](bool on) { emit freezeInputRequested(on); });
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
    // 无人值守开启常显徽标（Doc16 §8 风险对策：防"开关忘关长期裸奔"）：
    // 基础图标右上叠橙点，off 恢复基础图标
    unattended_on_ = on;
    if (base_icon_.isNull()) {
        return;  // 无系统图标可合成，退化为 tooltip 提示
    }
    if (!on) {
        tray_.setIcon(base_icon_);
    }
    else {
        QPixmap pm = base_icon_.pixmap(32, 32);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::white, 2));  // 白描边隔离底图
        p.setBrush(QColor(0xFF, 0x98, 0x00));
        p.drawEllipse(pm.width() - 11, 1, 10, 10);
        p.end();
        tray_.setIcon(QIcon(pm));
    }
    if (on) {
        tray_.setToolTip(QStringLiteral("SC 邻桌（无人值守开启中）"));
    }
    else if (!disconnect_action_->isEnabled()) {  // 会话中不改 tooltip
        tray_.setToolTip(QStringLiteral("SC 邻桌"));
    }
}

void TrayController::set_session_active(bool on, const QString& peer_alias) {
    if (on) {
        session_action_->setText(
            QStringLiteral("远程协助进行中：%1").arg(peer_alias));
        session_action_->setVisible(true);
        disconnect_action_->setEnabled(true);
        freeze_action_->setEnabled(true);
        tray_.setToolTip(QStringLiteral("SC 邻桌（远程协助中：%1）").arg(peer_alias));
    }
    else {
        session_action_->setVisible(false);
        disconnect_action_->setEnabled(false);
        freeze_action_->setEnabled(false);
        set_input_frozen_state(false);  // 会话结束回弹勾选（自动解除由 main 落地）
        tray_.setToolTip(unattended_on_ ? QStringLiteral("SC 邻桌（无人值守开启中）")
                                            : QStringLiteral("SC 邻桌"));
    }
}

void TrayController::set_input_frozen_state(bool on) {
    // 执行结果/自动解除的回执：blockSignals 防止 setChecked 触发 toggled
    // 再发 freezeInputRequested 造成回环
    const QSignalBlocker blocker(freeze_action_);
    freeze_action_->setChecked(on);
}

void TrayController::set_service_stopped(bool on) {
    stop_action_->setText(on ? QStringLiteral("恢复服务") : QStringLiteral("停止服务"));
    if (on) {
        tray_.setToolTip(QStringLiteral("SC 邻桌（服务已停止）"));
    }
    else if (!disconnect_action_->isEnabled()) {  // 会话中 tooltip 由会话态管理
        tray_.setToolTip(unattended_on_ ? QStringLiteral("SC 邻桌（无人值守开启中）")
                                        : QStringLiteral("SC 邻桌"));
    }
}

void TrayController::notify(const QString& title, const QString& body) {
    tray_.showMessage(title, body, QSystemTrayIcon::Information, 5000);
}

}  // namespace sc::localdesk::platform
