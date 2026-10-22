// ============================================================
// scToLocalDesk | src/ui/qwidget/popups/connect_confirm_dialog.h
// 连接确认弹窗（QWidget 版）：玻璃卡 + 弱阴影 + 缩放动画
// ============================================================
#pragma once

#include <QDialog>

class QCheckBox;
class QGraphicsOpacityEffect;
class QLabel;
class QPushButton;

namespace sc::localdesk::ui {

/// 连接确认弹窗（向日葵模式）：显示连接码，可勾选"记住此设备"
class ConnectConfirmDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ConnectConfirmDialog(QWidget* parent = nullptr);

    void setPeer(const QString& alias, const QString& ip, const QString& code);
    /// 用户是否勾选了"记住此设备，下次免确认"（V1.0 TODO 写入 TrustStore）
    [[nodiscard]] bool rememberChecked() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QLabel* text_{nullptr};
    QLabel* codeLbl_{nullptr};
    QLabel* ip_{nullptr};
    QCheckBox* rememberChk_{nullptr};
};

}  // namespace sc::localdesk::ui
