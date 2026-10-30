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
#include <functional>
#include <map>
#include <string>

#include "core/interface/iinput_injector.hpp"  // MouseAction（键鼠透传捕获）

class QComboBox;
class QFormLayout;
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
class BlacklistStore;  ///< 设备黑名单（Doc19 §3.4）
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
    /// 手动添加设备回调注入（Doc19 §6.3）：widgets_main 传 bridge.addManualPeer；
    /// 返回 false = 校验失败（页面弹提示）。未注入时按钮点击提示不可用
    void set_manual_add_handler(
        std::function<bool(const QString&, const QString&, uint16_t)> fn) {
        manual_add_ = std::move(fn);
    }
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
    /// 「手动添加设备」弹输入收集别名/IP[:端口] → 回调（Doc19 §6.3）
    void on_manual_add();
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
    std::function<bool(const QString&, const QString&, uint16_t)> manual_add_;
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

    /// 剪贴板同步开关配置注入（Doc19 §5 P2）：读写 config
    /// session.clipboard_sync（默认开）；widgets_main 装配后调用，
    /// 回放持久化真值到按钮 checked 态
    void bind_clipboard(foundation::IniConfig* ini, AppConfig* cfg);

signals:
    void disconnected();

public slots:
    /// 解码管线输出（queued 跨线程；有帧即走画面分支，无帧走占位文案）
    void on_frame(const QImage& img);

    /// 占位帧（Doc19 §4.2，queued 跨线程）：采集失败灰底+提示文字，
    /// 会话保持不断；正常帧到达（on_frame）复位
    void on_placeholder(bool locked);

    /// 会话建立（widgets_main 接 Evt_SessionStart）：快照角色/对端 +
    /// 恢复工具条——无帧会话也必须能断开（控制端无画面场景，实战踩坑）
    void on_session_started(const QString& alias);

    /// 只读模式徽标（P1，widgets_main 接 Evt_SessionStart）：控制端收到
    /// Approved 附加字节=1（对端未授权键鼠）时置位显示，会话关闭复位
    void on_read_only(bool on);

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
    QLabel* ro_badge_{nullptr};  ///< 只读徽标（顶部居中，对端未授权键鼠时显示）
    QPushButton* clip_btn_{nullptr};      ///< 剪贴板同步开关（工具条内，checkable）
    foundation::IniConfig* clip_ini_{nullptr};  ///< 开关落盘（bind_clipboard 注入）
    AppConfig* clip_cfg_{nullptr};              ///< 开关真值（bind_clipboard 注入）
    QImage frame_;
    bool placeholder_{false};        ///< 占位帧激活（画灰底+提示，不画图像）
    bool placeholder_locked_{false}; ///< 占位分支：锁屏文案 / 驱动异常文案
    RemoteSessionService* sessions_{nullptr};
    bool session_active_{false};  ///< 会话进行中（与有帧解耦：无帧也可断开）
    bool is_controller_{true};    ///< 本端角色（空态文案分流）
    QString peer_;                ///< 对端别名（空态文案展示）
};

/// 每行控件引用（QHash 值需完整类型）
struct FileRow {
    QProgressBar* bar{nullptr};
    QLabel* status{nullptr};
    QLabel* size{nullptr};
    QPushButton* cancel{nullptr};  ///< 排队态取消按钮（Doc19 §5 P2，仅排队中显示）
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
    /// 排队中条目取消（Doc19 §5 P2，widgets_main 转 FileService.cancel）
    void cancelRequested(const QString& file_id);

public slots:
    /// Evt_File* → 桥接信号（dir: 0 发送 1 接收）
    void on_file_queued(const QString& file_id, const QString& name, qint64 total);
    void on_file_progress(const QString& file_id, const QString& name,
                          qint64 transferred, qint64 total, int dir);
    void on_file_done(const QString& file_id, const QString& name, int dir);
    void on_file_failed(const QString& file_id, const QString& name, int dir);
    /// 可执行后缀拦截失败态（Doc19 §5）：显示「已拦截可执行文件」
    void on_file_blocked(const QString& file_id, const QString& name, int dir);

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

    /// 黑名单注入（Doc19 §3.4，P0）：设置页黑名单管理区块数据源
    void set_blacklist_store(BlacklistStore* bl) {
        blacklist_ = bl;
    }

    /// 信任缓存注入（Doc19 §10 信任管理子页）：列表/吊销/备注/清空
    void set_trust_store(TrustStore* ts) {
        trust_ = ts;
        if (trust_list_ != nullptr) {
            refresh_trust(trust_list_);
        }
    }

signals:
    /// 深色主题开关切换（主窗口统一 applyTheme：QSS + Dock 染色 + glyph）
    void themeChanged(bool dark);
    /// 无人值守开关变更（widgets_main 接线托盘徽标，Doc16 §7）
    void unattEnabledChanged(bool on);
    /// 同名覆盖策略变更（widgets_main 接线 FileService 热生效，Doc11 §4.2）
    void overwritePolicyChanged(const QString& policy);
    /// 可执行后缀拦截开关变更（widgets_main 接线 FileService 热生效，Doc19 §5）
    void blockExecutableChanged(bool on);

private:
    /// 控件变更统一入口：校验→更新 AppConfig→落盘（即时生效项在 QML 版由桥接服务）
    void on_changed(const QString& key, const QString& value);
    /// 分组卡：卡内含标题 + body 容器（传出供调用方挂 QFormLayout，
    /// 不能对已有布局的卡直接再 setLayout——Qt 会拒绝二设布局）
    QFrame* makeGroupCard(const QString& title, QWidget** bodyOut);
    /// 视图权限（Doc17 §7）：员工视图隐藏网络/会话/日志组与文件组高级行，
    /// IT 视图全量；bind_config 回放与 view_mode 切换时调用
    void apply_view_mode();
    /// 黑名单列表重载（blacklist.json → QListWidget；构造与变更后调用）
    void refresh_blacklist(QListWidget* list);
    /// 信任列表重载（trust.json → QListWidget；注入与变更后调用）
    void refresh_trust(QListWidget* list);

    foundation::IniConfig* ini_{nullptr};
    AppConfig* cfg_{nullptr};
    AliasService* aliases_{nullptr};
    BlacklistStore* blacklist_{nullptr};  ///< 黑名单（Doc19 §3.4，可空）
    TrustStore* trust_{nullptr};          ///< 信任缓存（Doc19 §10，可空）
    QListWidget* bl_list_{nullptr};       ///< 黑名单列表控件（变更后重载）
    QListWidget* trust_list_{nullptr};    ///< 信任列表控件（变更后重载）
    ToggleSwitch* viewSw_{nullptr};
    QFrame* grpNet_{nullptr};
    QFrame* grpSess_{nullptr};
    QFrame* grpLog_{nullptr};
    QFormLayout* fileForm_{nullptr};
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
    ToggleSwitch* blockExecSw_{nullptr};  ///< 可执行后缀拦截（Doc19 §5）
    QComboBox* owCombo_{nullptr};
    ToggleSwitch* unattSw_{nullptr};
    QLineEdit* lvEd_{nullptr};
    QLineEdit* logMaxEd_{nullptr};
    QLineEdit* logFilesEd_{nullptr};
};

}  // namespace sc::localdesk::ui
