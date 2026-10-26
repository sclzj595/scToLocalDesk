// ============================================================
// scToLocalDesk | src/ui/qwidget/pages/pages.h
// 业务页面（QWidget 版，与 QML 版逐项对齐）：
// 向日葵模式：本机连接码卡 + 设备信任角标 + 连接码输入页
// 纯色内容区禁玻璃（红线2），卡片 hover 主色描边
// ============================================================
#pragma once

#include <QAbstractButton>
#include <QImage>
#include <QWidget>

#include <cstdint>
#include <map>
#include <string>

#include "core/interface/iinput_injector.hpp"  // MouseAction（键鼠透传捕获）

class QGridLayout;
class QFrame;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QMouseEvent;
class QProgressBar;
class QPushButton;
class QTimer;
class QVBoxLayout;
class QWheelEvent;

namespace sc::localdesk {
struct AppConfig;  // 注意：定义处为 struct，前向声明必须一致（否则 MSVC 改名冲突）
class AliasService;
class RemoteSessionService;
class TrustStore;
namespace foundation {
class IniConfig;
}
}

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

    /// 服务注入（Doc18 §3.4）：AliasService 快照 + TrustStore 信任角标
    void bind_services(AliasService* aliases, TrustStore* trust);
    /// 本机信息卡真值（IdService 派生，widgets_main 调用）
    void set_self(const QString& name, const QString& code);
    /// 心跳快照对账：与在线表 diff，仅增删/更新变化卡（增量刷新，1s 调用）
    void apply_snapshot();

    /// 按连接码反查在线设备 IP（确认弹窗真值，Doc18 §3.4）；未知返回空
    QString deviceIp(const QString& code) const;

    /// 按连接码反查在线设备别名（连接码页发起会话用，PRD P7）；未知返回空
    QString deviceAlias(const QString& code) const;

signals:
    /// trusted=false 时由主窗口弹确认弹窗
    void connectRequested(const QString& aliasName, const QString& code, bool trusted);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* makeDeviceCard(const QString& name, const QString& code,
                            const QString& ip, bool busy, bool trusted);
    void reflow();
    AliasService* aliases_{nullptr};
    TrustStore* trust_{nullptr};
    struct Card {
        QWidget* widget{nullptr};
        uint32_t code{0};
        bool busy{false};
        bool trusted{false};
        std::string ip;
    };
    std::map<std::string, Card> cards_;  // alias → 卡（保序 diff）
    QLabel* self_name_lbl_{nullptr};
    QLabel* self_code_lbl_{nullptr};
    QGridLayout* grid_{nullptr};
};

/// 连接码连接页：9 位三段输入（自动跳格/粘贴分发/跨段退格）+ 最近连接列表
class ConnectCodePage final : public QWidget {
    Q_OBJECT

public:
    explicit ConnectCodePage(QWidget* parent = nullptr);

    /// 信任缓存注入（MainWindow 装配后调用，Doc18 §3.2）；随后刷新列表
    void set_trust_store(TrustStore* ts) {
        trust_ = ts;
        reload_history();
    }

signals:
    void connectRequested(const QString& code);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void tryConnect();
    bool handleSegKey(int idx, QKeyEvent* ev);
    void distributePaste(int idx, const QString& digits);
    void reload_history();  ///< 信任缓存 → 最近连接列表（空表显示占位）
    QLineEdit* seg_[3]{nullptr};
    QLabel* errHint_{nullptr};
    QListWidget* hist_list_{nullptr};
    QLabel* hist_empty_{nullptr};
    TrustStore* trust_{nullptr};
};

/// 远程画面页（画面区 + 底部悬浮工具条 + 键鼠透传捕获，Doc18 §4.4）
class RemoteViewPage final : public QWidget {
    Q_OBJECT

public:
    explicit RemoteViewPage(QWidget* parent = nullptr);

    /// DI：控制会话服务（键鼠事件发送出口），画面装配后调用
    void set_session(RemoteSessionService* s) { sessions_ = s; }

signals:
    void disconnected();

public slots:
    /// 解码管线输出（queued 跨线程；有帧即走画面分支，无帧走占位文案）
    void on_frame(const QImage& img);

    /// 会话关闭复位：清帧回空态、隐藏工具条（widgets_main 接 Evt_SessionClosed）
    void on_session_closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    void send_mouse(MouseAction action, const QPointF& local, int wheel = 0);

    QWidget* toolbar_{nullptr};
    QImage frame_;
    RemoteSessionService* sessions_{nullptr};
};

/// 每行控件引用（QHash 值需完整类型）
struct FileRow {
    QProgressBar* bar{nullptr};
    QLabel* status{nullptr};
    QLabel* size{nullptr};
    // 速度显示（PRD P10）：两次进度差/时间差 + 指数平滑
    qint64 last_x{0};
    qint64 last_t_ms{0};
    double speed_bps{0.0};
};

/// 文件传输列表页：真事件流动态行卡 + 主色进度条（完成绿/失败红）+
/// 发送入口（按钮选文件 / 拖拽入区，Doc18 §5）
class FileTransferPage final : public QWidget {
    Q_OBJECT

public:
    explicit FileTransferPage(QWidget* parent = nullptr);

signals:
    /// 选中/拖入的本地文件列表（widgets_main 转桥 sendFile）
    void filesPicked(const QStringList& paths);

public slots:
    /// Evt_File* → 桥接信号（dir: 0 发送 1 接收）
    void on_file_progress(const QString& file_id, const QString& name,
                          qint64 transferred, qint64 total, int dir);
    void on_file_done(const QString& file_id, const QString& name, int dir);
    void on_file_failed(const QString& file_id, const QString& name, int dir);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    FileRow& upsert_row(const QString& file_id, const QString& name, int dir);

    QWidget* host_{nullptr};
    QLabel* empty_{nullptr};
    QLabel* drag_hint_{nullptr};
    QHash<QString, FileRow> rows_;  ///< fileId → 行控件引用
};

/// 设置页：分组卡片（Doc17 全量项：通用/外观/网络/会话/文件/安全/日志）
class SettingsPage final : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr);

    /// 配置注入（Doc18 §3.3）：初始化控件真值并启用持久化（widgets_main 调用）
    void bind_config(foundation::IniConfig* ini, AppConfig* cfg);

    /// 别名服务注入（静态映射热生效，Doc17 §3.3；与 QML bridge 口径一致）
    void set_alias_service(AliasService* a) {
        aliases_ = a;
    }

signals:
    /// 深色主题开关切换（主窗口统一 applyTheme：QSS + Dock 染色 + glyph）
    void themeChanged(bool dark);
    /// 清空信任设备缓存（TrustStore::clear 由主窗口执行，Doc18 §3.2）
    void clearTrustRequested();

private:
    /// 控件变更统一入口：校验→更新 AppConfig→落盘（即时生效项在 QML 版由桥接服务）
    void on_changed(const QString& key, const QString& value);
    /// 分组卡：卡内含标题 + body 容器（传出供调用方挂 QFormLayout，
    /// 不能对已有布局的卡直接再 setLayout——Qt 会拒绝二设布局）
    QFrame* makeGroupCard(const QString& title, QWidget** bodyOut);

    foundation::IniConfig* ini_{nullptr};
    AppConfig* cfg_{nullptr};
    AliasService* aliases_{nullptr};
    QLineEdit* aliasEd_{nullptr};
    ToggleSwitch* autoStartSw_{nullptr};
    ToggleSwitch* minTraySw_{nullptr};
    ToggleSwitch* darkSw_{nullptr};
    ToggleSwitch* reduceSw_{nullptr};
    QLineEdit* portEd_{nullptr};
    QLineEdit* mcEd_{nullptr};
    ToggleSwitch* mcSw_{nullptr};
    QLineEdit* smEd_{nullptr};
    QLineEdit* hbEd_{nullptr};
    QLineEdit* fpsEd_{nullptr};
    QLineEdit* qEd_{nullptr};
    QLineEdit* dirEd_{nullptr};
    QLineEdit* chunkEd_{nullptr};
    ToggleSwitch* confirmSw_{nullptr};
    ToggleSwitch* safeSw_{nullptr};
    ToggleSwitch* unattSw_{nullptr};
    QLineEdit* lvEd_{nullptr};
    QLineEdit* logMaxEd_{nullptr};
    QLineEdit* logFilesEd_{nullptr};
};

}  // namespace sc::localdesk::ui
