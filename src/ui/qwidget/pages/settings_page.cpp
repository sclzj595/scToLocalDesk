// scToLocalDesk | 设置页（SettingsPage）实现（自 pages.cpp 原样拆出）
// 7 组字段 ↔ AppConfig（Doc18 §3.3）：bind_config 注入真值，on_changed 落盘。
// Widgets 版无服务接线（演示期约定）：即时生效项仅主题；其余重启后由 main 消费
#include "pages.h"
#include "page_kit.h"

#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include "../glass_theme.h"
#include "service/alias_service.hpp"
#include "service/app_config.hpp"
#include "foundation/config_parser.hpp"

namespace sc::localdesk::ui {

namespace {

QLineEdit* make_num_ed(QWidget* guard, int max_digits = 5) {
    auto* ed = new QLineEdit();
    ed->setFixedWidth(240);
    ed->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9]{0,%1}").arg(max_digits)), guard));
    return ed;
}

}  // namespace

// ---------------- 设置页 ----------------

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent) {
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { border: none; background: transparent; }"));
    auto* body = new QWidget();
    auto* lay = new QVBoxLayout(body);
    lay->setContentsMargins(28, 24, 28, 24);
    lay->setSpacing(16);

    lay->addWidget(makeTitle(QStringLiteral("设置")));

    // 控件变更统一走 on_changed(key, value)；bind_config 前调用为空操作
    auto bind_ed = [this](QLineEdit* ed, const QString& key) {
        connect(ed, &QLineEdit::editingFinished, this,
                [this, ed, key] { on_changed(key, ed->text()); });
    };
    auto bind_sw = [this](ToggleSwitch* sw, const QString& key) {
        connect(sw, &QAbstractButton::toggled, this,
                [this, key](bool on) { on_changed(key, on ? "true" : "false"); });
    };

    // ---- 通用（Doc17 §3.1） ----
    QWidget* genBody = nullptr;
    auto* grpGen = makeGroupCard(QStringLiteral("通用"), &genBody);
    auto* genForm = new QFormLayout(genBody);
    genForm->setLabelAlignment(Qt::AlignLeft);
    viewSw_ = new ToggleSwitch();
    bind_sw(viewSw_, QStringLiteral("general.view_mode"));
    genForm->addRow(QStringLiteral("IT 视图（显示全部高级配置）"), viewSw_);
    aliasEd_ = new QLineEdit();
    aliasEd_->setFixedWidth(240);
    bind_ed(aliasEd_, QStringLiteral("general.alias"));
    genForm->addRow(QStringLiteral("本机连接名"), aliasEd_);
    autoStartSw_ = new ToggleSwitch();
    bind_sw(autoStartSw_, QStringLiteral("app.auto_start"));
    genForm->addRow(QStringLiteral("开机自动启动"), autoStartSw_);
    minTraySw_ = new ToggleSwitch();
    bind_sw(minTraySw_, QStringLiteral("app.minimize_to_tray"));
    genForm->addRow(QStringLiteral("启动后最小化到托盘"), minTraySw_);
    lay->addWidget(grpGen);

    // ---- 外观（深色主题经 themeChanged 信号由主窗口统一应用） ----
    QWidget* lookBody = nullptr;
    auto* grpLook = makeGroupCard(QStringLiteral("外观"), &lookBody);
    auto* lookForm = new QFormLayout(lookBody);
    lookForm->setLabelAlignment(Qt::AlignLeft);
    darkSw_ = new ToggleSwitch();
    darkSw_->setChecked(GlassTheme::instance().isDark());
    connect(darkSw_, &QAbstractButton::toggled, this, &SettingsPage::themeChanged);
    bind_sw(darkSw_, QStringLiteral("appearance.theme"));
    lookForm->addRow(QStringLiteral("深色主题"), darkSw_);
    reduceSw_ = new ToggleSwitch();
    bind_sw(reduceSw_, QStringLiteral("appearance.reduce_transparency"));
    lookForm->addRow(QStringLiteral("降低透明度（无障碍）"), reduceSw_);
    lay->addWidget(grpLook);

    // ---- 网络（Doc17 §3.2） ----
    QWidget* netBody = nullptr;
    auto* grpNet = makeGroupCard(QStringLiteral("网络"), &netBody);
    auto* netForm = new QFormLayout(netBody);
    portEd_ = make_num_ed(this);
    bind_ed(portEd_, QStringLiteral("network.ws_port"));
    mcEd_ = new QLineEdit();
    mcEd_->setFixedWidth(240);
    bind_ed(mcEd_, QStringLiteral("network.multicast_addr"));
    mcSw_ = new ToggleSwitch();
    bind_sw(mcSw_, QStringLiteral("network.multicast_enabled"));
    smEd_ = new QLineEdit();
    smEd_->setFixedWidth(240);
    smEd_->setPlaceholderText(QStringLiteral("别名=IP:端口;别名2=IP2:端口2"));
    bind_ed(smEd_, QStringLiteral("network.static_map"));
    hbEd_ = make_num_ed(this, 2);
    bind_ed(hbEd_, QStringLiteral("network.heartbeat_interval_s"));
    netForm->addRow(QStringLiteral("会话端口"), portEd_);
    netForm->addRow(QStringLiteral("组播地址"), mcEd_);
    netForm->addRow(QStringLiteral("组播发现（关闭走静态映射，重启生效）"), mcSw_);
    netForm->addRow(QStringLiteral("静态映射"), smEd_);
    netForm->addRow(QStringLiteral("心跳间隔（秒）"), hbEd_);
    grpNet_ = grpNet;
    lay->addWidget(grpNet);

    // ---- 会话（Doc17 §3.3） ----
    QWidget* sessBody = nullptr;
    auto* grpSess = makeGroupCard(QStringLiteral("远程会话"), &sessBody);
    auto* sessForm = new QFormLayout(sessBody);
    fpsEd_ = make_num_ed(this, 2);
    bind_ed(fpsEd_, QStringLiteral("session.fps"));
    qEd_ = make_num_ed(this, 3);
    bind_ed(qEd_, QStringLiteral("session.jpeg_quality"));
    sessForm->addRow(QStringLiteral("帧率（fps）"), fpsEd_);
    sessForm->addRow(QStringLiteral("JPEG 画质"), qEd_);
    grpSess_ = grpSess;
    lay->addWidget(grpSess);

    // ---- 文件（Doc17 §3.4） ----
    QWidget* fileBody = nullptr;
    auto* grpFile = makeGroupCard(QStringLiteral("文件传输"), &fileBody);
    auto* fileForm = new QFormLayout(fileBody);
    dirEd_ = new QLineEdit();
    dirEd_->setFixedWidth(240);
    bind_ed(dirEd_, QStringLiteral("file.save_path"));
    chunkEd_ = make_num_ed(this, 4);
    bind_ed(chunkEd_, QStringLiteral("file.chunk_size_kb"));
    fileForm->addRow(QStringLiteral("接收目录"), dirEd_);
    fileForm->addRow(QStringLiteral("分片大小（KB）"), chunkEd_);
    confirmSw_ = new ToggleSwitch();
    bind_sw(confirmSw_, QStringLiteral("file.confirm_before_transfer"));
    fileForm->addRow(QStringLiteral("接收前需确认"), confirmSw_);
    safeSw_ = new ToggleSwitch();
    bind_sw(safeSw_, QStringLiteral("file.safe_path"));
    fileForm->addRow(QStringLiteral("文件名安全校验"), safeSw_);
    owCombo_ = new QComboBox();
    owCombo_->setFixedWidth(240);
    owCombo_->addItem(QStringLiteral("每次询问（推荐）"), QStringLiteral("ask"));
    owCombo_->addItem(QStringLiteral("直接覆盖"), QStringLiteral("overwrite"));
    owCombo_->addItem(QStringLiteral("自动重命名 (1)"), QStringLiteral("rename"));
    connect(owCombo_, &QComboBox::activated, this, [this](int idx) {
        on_changed(QStringLiteral("file.overwrite"),
                   owCombo_->itemData(idx).toString());
    });
    fileForm->addRow(QStringLiteral("同名覆盖策略"), owCombo_);
    fileForm_ = fileForm;
    lay->addWidget(grpFile);

    // ---- 安全（清空信任缓存带即时反馈） ----
    QWidget* secBody = nullptr;
    auto* grpSec = makeGroupCard(QStringLiteral("安全"), &secBody);
    auto* secForm = new QFormLayout(secBody);
    unattSw_ = new ToggleSwitch();
    bind_sw(unattSw_, QStringLiteral("security.unattended_enabled"));
    secForm->addRow(QStringLiteral("允许无人值守连接"), unattSw_);
    auto* pwdEd = new QLineEdit();
    pwdEd->setEchoMode(QLineEdit::Password);
    pwdEd->setPlaceholderText(QStringLiteral("6~32 位"));
    pwdEd->setFixedWidth(240);
    pwdEd->setToolTip(QStringLiteral("凭据派生（PBKDF2）与即时生效为 V1.0 任务"));
    secForm->addRow(QStringLiteral("无人值守密码"), pwdEd);
    auto* trustBtn = new QPushButton(QStringLiteral("清空信任设备缓存"));
    trustBtn->setObjectName(QStringLiteral("secondaryBtn"));
    trustBtn->setCursor(Qt::PointingHandCursor);
    connect(trustBtn, &QPushButton::clicked, this, [this, trustBtn] {
        emit clearTrustRequested();   // TrustStore::clear 由主窗口执行（Doc18 §3.2）
        trustBtn->setText(QStringLiteral("已清空 ✓"));
        trustBtn->setEnabled(false);
        QTimer::singleShot(1600, trustBtn, [trustBtn] {
            trustBtn->setText(QStringLiteral("清空信任设备缓存"));
            trustBtn->setEnabled(true);
        });
    });
    secForm->addRow(QString(), trustBtn);
    lay->addWidget(grpSec);

    // ---- 日志（Doc17 §3.6） ----
    QWidget* logBody = nullptr;
    auto* grpLog = makeGroupCard(QStringLiteral("日志"), &logBody);
    auto* logForm = new QFormLayout(logBody);
    lvEd_ = new QLineEdit();
    lvEd_->setFixedWidth(240);
    lvEd_->setPlaceholderText(QStringLiteral("trace / debug / info / warn / error"));
    bind_ed(lvEd_, QStringLiteral("log.level"));
    logMaxEd_ = make_num_ed(this, 4);
    bind_ed(logMaxEd_, QStringLiteral("log.max_size_mb"));
    logFilesEd_ = make_num_ed(this, 2);
    bind_ed(logFilesEd_, QStringLiteral("log.max_files"));
    logForm->addRow(QStringLiteral("日志级别"), lvEd_);
    logForm->addRow(QStringLiteral("日志上限（MB）"), logMaxEd_);
    logForm->addRow(QStringLiteral("日志文件数"), logFilesEd_);
    grpLog_ = grpLog;
    lay->addWidget(grpLog);

    lay->addStretch();
    scroll->setWidget(body);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
}

void SettingsPage::bind_config(foundation::IniConfig* ini, AppConfig* cfg) {
    ini_ = ini;
    cfg_ = cfg;
    if (cfg_ == nullptr) {
        return;
    }
    // 回放真值（setChecked/setText 不触发用户语义信号，仅 toggled 同步主题一次）
    aliasEd_->setText(QString::fromStdString(cfg_->alias));
    autoStartSw_->setChecked(cfg_->auto_start);
    minTraySw_->setChecked(cfg_->minimize_to_tray);
    darkSw_->setChecked(cfg_->appearance_theme == "dark");
    reduceSw_->setChecked(cfg_->appearance_reduce_transparency);
    portEd_->setText(QString::number(cfg_->ws_port));
    mcEd_->setText(QString::fromStdString(cfg_->multicast_addr));
    mcSw_->setChecked(cfg_->multicast_enabled);
    smEd_->setText(QString::fromStdString(cfg_->static_map));
    hbEd_->setText(QString::number(cfg_->heartbeat_interval_s));
    fpsEd_->setText(QString::number(cfg_->fps));
    qEd_->setText(QString::number(cfg_->jpeg_quality));
    dirEd_->setText(QString::fromStdString(cfg_->save_path));
    chunkEd_->setText(QString::number(cfg_->chunk_size_kb));
    confirmSw_->setChecked(cfg_->confirm_before_transfer);
    safeSw_->setChecked(cfg_->file_safe_path);
    // 覆盖策略回放：按 userData 定位档位（ask/overwrite/rename）
    const QString ow = QString::fromStdString(cfg_->overwrite);
    for (int i = 0; i < owCombo_->count(); ++i) {
        if (owCombo_->itemData(i).toString() == ow) {
            owCombo_->setCurrentIndex(i);
            break;
        }
    }
    unattSw_->setChecked(cfg_->unattended_enabled);
    viewSw_->setChecked(cfg_->view_mode == "it");
    lvEd_->setText(QString::fromStdString(cfg_->log_level));
    logMaxEd_->setText(QString::number(cfg_->log_max_size_mb));
    logFilesEd_->setText(QString::number(cfg_->log_max_files));
    apply_view_mode();  // 回放即生效（员工视图启动收起高级行）
}

void SettingsPage::on_changed(const QString& key, const QString& value) {
    if (ini_ == nullptr || cfg_ == nullptr) {
        return;  // 未注入（演示期单跑页面）不落盘
    }
    const std::string k = key.toStdString();
    const std::string v = value.toStdString();
    bool num_ok = false;
    // 校验口径与 QtEventBridge::settingsSetValue 一致（双端契约）
    if (k == "general.alias") {
        if (v.empty()) {
            return;
        }
        cfg_->alias = v;
    }
    else if (k == "app.auto_start") {
        cfg_->auto_start = (value == QStringLiteral("true"));
    }
    else if (k == "app.minimize_to_tray") {
        cfg_->minimize_to_tray = (value == QStringLiteral("true"));
    }
    else if (k == "appearance.theme") {
        cfg_->appearance_theme = v;  // 调用方保证仅 dark/light（开关语义）
    }
    else if (k == "appearance.reduce_transparency") {
        cfg_->appearance_reduce_transparency = (value == QStringLiteral("true"));
    }
    else if (k == "network.ws_port") {
        const int n = value.toInt(&num_ok);
        if (!num_ok || n < 1024 || n > 65535) {
            return;
        }
        cfg_->ws_port = n;
    }
    else if (k == "network.multicast_addr") {
        cfg_->multicast_addr = v;
    }
    else if (k == "network.multicast_enabled") {
        cfg_->multicast_enabled = (value == QStringLiteral("true"));
    }
    else if (k == "network.static_map") {
        // 校验口径与 QtEventBridge::settingsSetValue 一致；合法即热生效
        const auto table = parse_static_map(v);
        if (!v.empty() && table.empty()) {
            return;  // 畸形格式拒绝落盘
        }
        cfg_->static_map = v;
        if (aliases_ != nullptr) {
            aliases_->set_static_map(table);
        }
    }
    else if (k == "network.heartbeat_interval_s") {
        const int n = value.toInt(&num_ok);
        if (!num_ok || n < 1 || n > 60) {
            return;
        }
        cfg_->heartbeat_interval_s = n;
    }
    else if (k == "session.fps") {
        const int n = value.toInt(&num_ok);
        if (!num_ok || (n != 10 && n != 15 && n != 24 && n != 30)) {
            return;
        }
        cfg_->fps = n;
    }
    else if (k == "session.jpeg_quality") {
        const int n = value.toInt(&num_ok);
        if (!num_ok || n < 1 || n > 100) {
            return;
        }
        cfg_->jpeg_quality = n;
    }
    else if (k == "file.save_path") {
        cfg_->save_path = v;
    }
    else if (k == "file.chunk_size_kb") {
        const int n = value.toInt(&num_ok);
        if (!num_ok || n < 16 || n > 4096) {
            return;
        }
        cfg_->chunk_size_kb = n;
    }
    else if (k == "file.confirm_before_transfer") {
        cfg_->confirm_before_transfer = (value == QStringLiteral("true"));
    }
    else if (k == "file.safe_path") {
        cfg_->file_safe_path = (value == QStringLiteral("true"));
    }
    else if (k == "file.overwrite") {
        // 同名覆盖策略（Doc11 §4.2）：热生效走信号（widgets_main 接 files_）
        cfg_->overwrite = v;
        emit overwritePolicyChanged(value);
    }
    else if (k == "security.unattended_enabled") {
        cfg_->unattended_enabled = (value == QStringLiteral("true"));
        emit unattEnabledChanged(cfg_->unattended_enabled);
    }
    else if (k == "general.view_mode") {
        // Doc17 §7 视图权限：bind_sw 传 true/false → 映射 it/employee，热生效
        cfg_->view_mode = (value == QStringLiteral("true")) ? "it" : "employee";
        apply_view_mode();
    }
    else if (k == "log.level") {
        if (v != "trace" && v != "debug" && v != "info" && v != "warn" && v != "error") {
            return;
        }
        cfg_->log_level = v;
    }
    else if (k == "log.max_size_mb" || k == "log.max_files") {
        const int n = value.toInt(&num_ok);
        if (!num_ok || n < 1 || n > 1024) {
            return;
        }
        if (k == "log.max_size_mb") {
            cfg_->log_max_size_mb = n;
        }
        else {
            cfg_->log_max_files = n;
        }
    }
    else {
        return;
    }
    cfg_->save(*ini_);
    (void)ini_->save();
}

QFrame* SettingsPage::makeGroupCard(const QString& title, QWidget** bodyOut) {
    auto* card = new QFrame();
    card->setObjectName(QStringLiteral("deviceCard"));
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(18, 14, 18, 16);
    lay->setSpacing(10);
    auto* t = new QLabel(title);
    t->setObjectName(QStringLiteral("cardTitle"));
    lay->addWidget(t);
    // body 容器：调用方在其上挂 QFormLayout（卡布局已占用，不可二设）
    auto* bodyWidget = new QWidget();
    bodyWidget->setStyleSheet(QStringLiteral("background: transparent;"));
    lay->addWidget(bodyWidget);
    *bodyOut = bodyWidget;
    return card;
}

void SettingsPage::apply_view_mode() {
    if (cfg_ == nullptr) {
        return;
    }
    const bool it = (cfg_->view_mode == "it");
    // Doc17 §7：网络/会话/日志组员工隐藏；文件组仅留接收目录；
    // 安全组员工可见（无人值守开关+密码，员工也可为自己开启）
    if (grpNet_ != nullptr) {
        grpNet_->setVisible(it);
    }
    if (grpSess_ != nullptr) {
        grpSess_->setVisible(it);
    }
    if (grpLog_ != nullptr) {
        grpLog_->setVisible(it);
    }
    if (fileForm_ != nullptr) {
        fileForm_->setRowVisible(chunkEd_, it);
        fileForm_->setRowVisible(confirmSw_, it);
        fileForm_->setRowVisible(safeSw_, it);
    }
}

}  // namespace sc::localdesk::ui
