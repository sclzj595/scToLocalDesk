// ============================================================
// scToLocalDesk | src/ui/qwidget/pages/pages.h
// 业务页面（QWidget 版，与 QML 版逐项对齐）：
// 向日葵模式：本机连接码卡 + 设备信任角标 + 连接码输入页
// 纯色内容区禁玻璃（红线2），卡片 hover 主色描边
// ============================================================
#pragma once

#include <QAbstractButton>
#include <QWidget>

class QGridLayout;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTimer;
class QVBoxLayout;

namespace sc::localdesk::ui {

/// 滑动开关（对齐 QML SwitchRow：42×24 轨道 + 18px 圆钮，120ms 动画）
class ToggleSwitch final : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(qreal knob READ knob WRITE setKnob)

public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    qreal knob() const { return knob_; }
    void setKnob(qreal k) {
        knob_ = k;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    qreal knob_{0.0};  // 0~1 圆钮动画进度
    bool hover_{false};
};

/// 设备发现列表页：顶部本机信息卡（连接名+9位连接码+复制）+ 设备卡网格
class DeviceListPage final : public QWidget {
    Q_OBJECT

public:
    explicit DeviceListPage(QWidget* parent = nullptr);

signals:
    /// trusted=false 时由主窗口弹确认弹窗
    void connectRequested(const QString& aliasName, const QString& code, bool trusted);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* makeDeviceCard(const QString& name, const QString& user,
                            const QString& code, const QString& ip,
                            const QString& stateText, const QColor& stateColor,
                            bool trusted);
    QString self_name_;
    QString self_code_;
    QGridLayout* grid_{nullptr};
};

/// 连接码连接页：9 位三段输入（自动跳格）+ 最近连接列表
class ConnectCodePage final : public QWidget {
    Q_OBJECT

public:
    explicit ConnectCodePage(QWidget* parent = nullptr);

signals:
    void connectRequested(const QString& code);

private:
    void tryConnect();
    QLineEdit* seg_[3]{nullptr};
    QLabel* errHint_{nullptr};
};

/// 远程画面页（画面区 + 底部悬浮工具条）
class RemoteViewPage final : public QWidget {
    Q_OBJECT

public:
    explicit RemoteViewPage(QWidget* parent = nullptr);

signals:
    void disconnected();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QWidget* toolbar_{nullptr};
};

/// 文件传输列表页：行卡 + 主色进度条 + 状态色（完成绿/失败红）
class FileTransferPage final : public QWidget {
    Q_OBJECT

public:
    explicit FileTransferPage(QWidget* parent = nullptr);

private:
    QWidget* makeRow(const QString& arrow, const QString& name,
                     int progress, const QString& status, const QColor& statusColor);
    QVBoxLayout* rows_{nullptr};
};

/// 设置页：分组卡片（Doc17 全量项：通用/外观/网络/会话/文件/安全/日志）
class SettingsPage final : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr);

signals:
    /// 深色主题开关切换（主窗口统一 applyTheme：QSS + Dock 染色 + glyph）
    void themeChanged(bool dark);

private:
    /// 分组卡：卡内含标题 + body 容器（传出供调用方挂 QFormLayout，
    /// 不能对已有布局的卡直接再 setLayout——Qt 会拒绝二设布局）
    QFrame* makeGroupCard(const QString& title, QWidget** bodyOut);
};

}  // namespace sc::localdesk::ui
