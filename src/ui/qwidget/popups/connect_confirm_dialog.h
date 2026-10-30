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
    /// 用户是否勾选了"记住此设备，下次免确认直连"（V1.0 TODO 写入 TrustStore）
    bool rememberChecked() const;
    /// 是否授权对方键鼠控制（"允许对方键鼠控制"勾选态；取消 = 只读模式，
    /// P1 被控端审批决定，随 approve(read_only) 进审批链路）
    bool inputAllowed() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QLabel* text_{nullptr};
    QLabel* codeLbl_{nullptr};
    QLabel* ip_{nullptr};
    QCheckBox* rememberChk_{nullptr};
    QCheckBox* allowInputChk_{nullptr};  ///< 允许对方键鼠控制（默认勾选；取消=只读）
};

}  // namespace sc::localdesk::ui
