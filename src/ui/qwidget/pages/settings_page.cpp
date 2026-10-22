// scToLocalDesk | 设置页（SettingsPage）实现（自 pages.cpp 原样拆出）
#include "pages.h"
#include "page_kit.h"

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

namespace sc::localdesk::ui {

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

    // ---- 通用（Doc17 §3.1） ----
    QWidget* genBody = nullptr;
    auto* grpGen = makeGroupCard(QStringLiteral("通用"), &genBody);
    auto* genForm = new QFormLayout(genBody);
    genForm->setLabelAlignment(Qt::AlignLeft);
    auto* aliasEd = new QLineEdit(QStringLiteral("zhangsan@SC-DESK"));
    aliasEd->setFixedWidth(240);
    genForm->addRow(QStringLiteral("本机连接名"), aliasEd);  // TODO(V1.0) config general.alias
    auto* autoStartSw = new ToggleSwitch();
    genForm->addRow(QStringLiteral("开机自动启动"), autoStartSw);          // TODO general.auto_start
    auto* minTraySw = new ToggleSwitch();
    genForm->addRow(QStringLiteral("启动后最小化到托盘"), minTraySw);      // TODO general.start_minimized
    lay->addWidget(grpGen);

    // ---- 外观（深色主题经 themeChanged 信号由主窗口统一应用） ----
    QWidget* lookBody = nullptr;
    auto* grpLook = makeGroupCard(QStringLiteral("外观"), &lookBody);
    auto* lookForm = new QFormLayout(lookBody);
    lookForm->setLabelAlignment(Qt::AlignLeft);
    auto* darkSw = new ToggleSwitch();
    darkSw->setChecked(GlassTheme::instance().isDark());
    connect(darkSw, &QAbstractButton::toggled, this, &SettingsPage::themeChanged);
    lookForm->addRow(QStringLiteral("深色主题"), darkSw);
    auto* reduceSw = new ToggleSwitch();                                  // TODO appearance.reduce_transparency
    lookForm->addRow(QStringLiteral("降低透明度（无障碍）"), reduceSw);
    lay->addWidget(grpLook);

    // ---- 网络（Doc17 §3.2） ----
    QWidget* netBody = nullptr;
    auto* grpNet = makeGroupCard(QStringLiteral("网络"), &netBody);
    auto* netForm = new QFormLayout(netBody);
    auto* portEd = new QLineEdit(QStringLiteral("8899"));
    portEd->setFixedWidth(240);
    portEd->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9]{0,5}")), this));
    auto* mcEd = new QLineEdit(QStringLiteral("239.255.88.99"));
    mcEd->setFixedWidth(240);
    auto* hbEd = new QLineEdit(QStringLiteral("3"));
    hbEd->setFixedWidth(240);
    hbEd->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9]{0,2}")), this));
    netForm->addRow(QStringLiteral("会话端口"), portEd);      // TODO network.ws_port
    netForm->addRow(QStringLiteral("组播地址"), mcEd);        // TODO network.multicast_addr
    netForm->addRow(QStringLiteral("心跳间隔（秒）"), hbEd);  // TODO network.heartbeat_interval_s
    lay->addWidget(grpNet);

    // ---- 会话（Doc17 §3.3） ----
    QWidget* sessBody = nullptr;
    auto* grpSess = makeGroupCard(QStringLiteral("远程会话"), &sessBody);
    auto* sessForm = new QFormLayout(sessBody);
    auto* fpsEd = new QLineEdit(QStringLiteral("24"));
    fpsEd->setFixedWidth(240);
    auto* qEd = new QLineEdit(QStringLiteral("80"));
    qEd->setFixedWidth(240);
    sessForm->addRow(QStringLiteral("帧率（fps）"), fpsEd);   // TODO session.fps
    sessForm->addRow(QStringLiteral("JPEG 画质"), qEd);       // TODO session.jpeg_quality
    lay->addWidget(grpSess);

    // ---- 文件（Doc17 §3.4） ----
    QWidget* fileBody = nullptr;
    auto* grpFile = makeGroupCard(QStringLiteral("文件传输"), &fileBody);
    auto* fileForm = new QFormLayout(fileBody);
    auto* dirEd = new QLineEdit(QStringLiteral("C:/Users/11/Desktop"));
    dirEd->setFixedWidth(240);
    auto* chunkEd = new QLineEdit(QStringLiteral("512"));
    chunkEd->setFixedWidth(240);
    fileForm->addRow(QStringLiteral("接收目录"), dirEd);          // TODO file.save_path
    fileForm->addRow(QStringLiteral("分片大小（KB）"), chunkEd);  // TODO file.chunk_size_kb
    auto* confirmSw = new ToggleSwitch();
    confirmSw->setChecked(true);
    fileForm->addRow(QStringLiteral("接收前需确认"), confirmSw);  // TODO file.confirm_before_transfer
    lay->addWidget(grpFile);

    // ---- 安全（清空信任缓存带即时反馈） ----
    QWidget* secBody = nullptr;
    auto* grpSec = makeGroupCard(QStringLiteral("安全"), &secBody);
    auto* secForm = new QFormLayout(secBody);
    auto* unattSw = new ToggleSwitch();
    secForm->addRow(QStringLiteral("允许无人值守连接"), unattSw);  // TODO security.unattended_enabled
    auto* pwdEd = new QLineEdit();
    pwdEd->setEchoMode(QLineEdit::Password);
    pwdEd->setPlaceholderText(QStringLiteral("6~32 位"));
    pwdEd->setFixedWidth(240);
    secForm->addRow(QStringLiteral("无人值守密码"), pwdEd);   // TODO security.unattended_password
    auto* trustBtn = new QPushButton(QStringLiteral("清空信任设备缓存"));
    trustBtn->setObjectName(QStringLiteral("secondaryBtn"));
    trustBtn->setCursor(Qt::PointingHandCursor);
    connect(trustBtn, &QPushButton::clicked, this, [trustBtn] {
        // TODO(V1.0) TrustStore::clearAll()
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
    auto* lvEd = new QLineEdit(QStringLiteral("info"));
    lvEd->setFixedWidth(240);
    lvEd->setPlaceholderText(QStringLiteral("trace / debug / info / warn / error"));
    logForm->addRow(QStringLiteral("日志级别"), lvEd);            // TODO log.level
    auto* consoleSw = new ToggleSwitch();
    consoleSw->setChecked(true);
    logForm->addRow(QStringLiteral("控制台输出"), consoleSw);     // TODO log.console
    lay->addWidget(grpLog);

    lay->addStretch();
    scroll->setWidget(body);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
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

}  // namespace sc::localdesk::ui
