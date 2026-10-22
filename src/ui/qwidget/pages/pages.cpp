// ============================================================
// scToLocalDesk | src/ui/qwidget/pages/pages.cpp
// QWidget 版页面实现（向日葵模式，与 QML 版逐项对齐）
// ============================================================
#include "pages.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QEvent>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include "../glass_theme.h"

namespace sc::localdesk::ui {

namespace {

QLabel* makeTitle(const QString& text) {
    auto* lbl = new QLabel(text);
    lbl->setObjectName(QStringLiteral("titleLabel"));
    return lbl;
}

QLabel* makeHint(const QString& text) {
    auto* lbl = new QLabel(text);
    lbl->setObjectName(QStringLiteral("hintLabel"));
    return lbl;
}

/// 头像圆标（淡紫底 + 主色首字）
QLabel* makeAvatar(const QString& ch, int size) {
    auto* av = new QLabel(ch);
    av->setFixedSize(size, size);
    av->setAlignment(Qt::AlignCenter);
    av->setStyleSheet(QStringLiteral(
        "background: rgba(157,78,221,0.14); color: #9D4EDD; border-radius: %1px; "
        "font-size: %2px; font-weight: 600;")
        .arg(size / 2)
        .arg(size * 0.4, 0, 'f', 0));
    return av;
}

/// 状态点（●字符染色，Doc13 §2.1 辅助色语义）
QLabel* makeStateDot(const QColor& c) {
    auto* dot = new QLabel(QStringLiteral("●"));
    dot->setStyleSheet(QStringLiteral("color: %1; font-size: 9px;")
                           .arg(c.name(QColor::HexRgb)));
    return dot;
}

}  // namespace

// ---------------- 滑动开关（对齐 QML SwitchRow） ----------------

ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(42, 24);
    auto* anim = new QPropertyAnimation(this, "knob", this);
    anim->setDuration(120);   // Doc13 §6：120ms
    connect(this, &QAbstractButton::toggled, this, [this, anim](bool on) {
        anim->stop();
        anim->setStartValue(knob_);
        anim->setEndValue(on ? 1.0 : 0.0);
        anim->start();
    });
}

void ToggleSwitch::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // 轨道：选中主色 / 未选分割线色，hover 微亮
    auto track = isChecked() ? GlassTheme::instance().primary()
                             : GlassTheme::instance().divider();
    if (hover_) {
        track = track.lighter(110);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(track);
    p.drawRoundedRect(rect(), 12, 12);
    // 圆钮：白色，位置随 knob 动画
    const qreal x = 3.0 + knob_ * (width() - 24.0);
    p.setBrush(QColor(Qt::white));
    p.drawEllipse(QRectF(x, 3.0, 18.0, 18.0));
}

void ToggleSwitch::enterEvent(QEnterEvent* e) {
    hover_ = true;
    update();
    QAbstractButton::enterEvent(e);
}

void ToggleSwitch::leaveEvent(QEvent* e) {
    hover_ = false;
    update();
    QAbstractButton::leaveEvent(e);
}

// ---------------- 设备列表页 ----------------

DeviceListPage::DeviceListPage(QWidget* parent) : QWidget(parent) {
    // 演示数据（V1.0 由 bridge.selfAlias / IdService / TrustStore 驱动）
    self_name_ = QStringLiteral("zhangsan@SC-DESK");
    self_code_ = QStringLiteral("538194207");

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(28, 24, 28, 24);
    lay->setSpacing(18);

    // ---- 本机信息卡 ----
    auto* selfCard = new QFrame();
    selfCard->setObjectName(QStringLiteral("deviceCard"));
    selfCard->setFixedHeight(96);
    auto* selfLay = new QHBoxLayout(selfCard);
    selfLay->setContentsMargins(16, 14, 16, 14);
    selfLay->setSpacing(18);
    selfLay->addWidget(makeAvatar(QStringLiteral("本"), 56));

    auto* nameCol = new QVBoxLayout();
    nameCol->setSpacing(3);
    nameCol->addWidget(makeHint(QStringLiteral("本机连接名")));
    auto* nameLbl = new QLabel(self_name_);
    nameLbl->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 600; color: %1;")
                               .arg(GlassTheme::instance().textMain().name(QColor::HexRgb)));
    nameCol->addWidget(nameLbl);
    selfLay->addLayout(nameCol);
    selfLay->addStretch();

    auto* codeCol = new QVBoxLayout();
    codeCol->setSpacing(3);
    codeCol->addWidget(makeHint(QStringLiteral("本机连接码")));
    auto* codeLbl = new QLabel(QStringLiteral("538 194 207"));
    QFont codeFont = codeLbl->font();
    codeFont.setPointSizeF(12.0);
    codeFont.setBold(true);
    codeFont.setLetterSpacing(QFont::AbsoluteSpacing, 3.0);
    codeLbl->setFont(codeFont);
    codeLbl->setStyleSheet(QStringLiteral("color: %1;")
                               .arg(GlassTheme::instance().primary().name(QColor::HexRgb)));
    codeCol->addWidget(codeLbl);
    selfLay->addLayout(codeCol);

    auto* copyBtn = new QPushButton(QStringLiteral("复制"));
    copyBtn->setObjectName(QStringLiteral("secondaryBtn"));
    copyBtn->setStyleSheet(QStringLiteral(
        "color: %1; border: 1px solid rgba(157,78,221,0.45); padding: 6px 16px;")
        .arg(GlassTheme::instance().primary().name(QColor::HexRgb)));
    connect(copyBtn, &QPushButton::clicked, this, [this, copyBtn] {
        QApplication::clipboard()->setText(self_code_);
        // 复制反馈：按钮文字临时变化（对齐 QML 版）
        copyBtn->setText(QStringLiteral("已复制 ✓"));
        QTimer::singleShot(1600, copyBtn, [copyBtn] {
            copyBtn->setText(QStringLiteral("复制"));
        });
    });
    selfLay->addWidget(copyBtn);
    lay->addWidget(selfCard);

    // ---- 标题行 ----
    auto* head = new QHBoxLayout();
    head->addWidget(makeTitle(QStringLiteral("局域网设备")));
    head->addStretch();
    head->addWidget(makeHint(QStringLiteral("首次连接需对方确认，确认后免验证直连")));
    lay->addLayout(head);

    // ---- 设备卡网格 ----
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { border: none; background: transparent; }"));
    auto* gridHost = new QWidget();
    grid_ = new QGridLayout(gridHost);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setSpacing(16);
    scroll->setWidget(gridHost);
    lay->addWidget(scroll, 1);

    struct Demo {
        const char* name;
        const char* user;
        const char* code;
        const char* ip;
        const char* state;
        const char* color;
        bool trusted;
    };
    const Demo demos[] = {
        {"设计部-张工", "zhang.s@DESIGN-01", "482 130 556", "192.168.1.23",
         "可连接", "#3A86FF", true},
        {"运维室-李工", "li.y@OPS-PC", "107 662 384", "192.168.1.45",
         "会话中", "#4CC9F0", false},
        {"会议室-终端", "meeting@CONF-TV", "903 245 117", "192.168.1.88",
         "离线", "#F72585", true},
    };
    for (int i = 0; i < 3; ++i) {
        grid_->addWidget(makeDeviceCard(demos[i].name, demos[i].user, demos[i].code,
                                        demos[i].ip, demos[i].state,
                                        QColor(demos[i].color), demos[i].trusted),
                         i / 3, i % 3);
    }
    grid_->setColumnStretch(0, 1);
    grid_->setColumnStretch(1, 1);
    grid_->setColumnStretch(2, 1);
    grid_->setRowStretch(1, 1);
}

QWidget* DeviceListPage::makeDeviceCard(const QString& name, const QString& user,
                                        const QString& code, const QString& ip,
                                        const QString& stateText,
                                        const QColor& stateColor, bool trusted) {
    auto* card = new QFrame();
    card->setObjectName(QStringLiteral("deviceCard"));
    card->setFixedHeight(104);
    card->setCursor(Qt::PointingHandCursor);
    card->setProperty("devName", name);
    card->setProperty("devCode", code);
    card->setProperty("devTrusted", trusted);
    card->setAttribute(Qt::WA_Hover, true);
    card->installEventFilter(this);

    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(14, 12, 14, 12);
    lay->setSpacing(6);

    auto* top = new QHBoxLayout();
    top->setSpacing(12);
    top->addWidget(makeAvatar(name.left(1), 40));

    auto* nameCol = new QVBoxLayout();
    nameCol->setSpacing(1);
    auto* nameLbl = new QLabel(name);
    nameLbl->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 600; color: %1;")
                               .arg(GlassTheme::instance().textMain().name(QColor::HexRgb)));
    nameCol->addWidget(nameLbl);
    auto* userLbl = new QLabel(user);
    userLbl->setStyleSheet(QStringLiteral("font-size: 12px; color: %1;")
                               .arg(GlassTheme::instance().textHint().name(QColor::HexRgb)));
    nameCol->addWidget(userLbl);
    top->addLayout(nameCol);
    top->addStretch();

    // 信任角标（向日葵模式：确认后才有记忆缓存）
    auto* badge = new QLabel(trusted ? QStringLiteral("已信任") : QStringLiteral("待确认"));
    badge->setStyleSheet(QStringLiteral(
        "font-size: 11px; padding: 3px 8px; border-radius: 4px; color: %1; "
        "background: %2;")
        .arg(trusted ? GlassTheme::instance().accentGreen().name(QColor::HexRgb)
                     : GlassTheme::instance().textHint().name(QColor::HexRgb))
        .arg(trusted ? "rgba(76,201,240,0.12)" : "rgba(110,118,129,0.10)"));
    top->addWidget(badge);
    lay->addLayout(top);

    auto* bottom = new QHBoxLayout();
    bottom->setSpacing(6);
    auto* codeLbl = new QLabel(code);
    QFont cf = codeLbl->font();
    cf.setBold(true);
    cf.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    codeLbl->setFont(cf);
    codeLbl->setStyleSheet(QStringLiteral("font-size: 13px; color: %1;")
                               .arg(GlassTheme::instance().primary().name(QColor::HexRgb)));
    bottom->addWidget(codeLbl);
    bottom->addStretch();
    bottom->addWidget(makeStateDot(stateColor));
    auto* ipLbl = new QLabel(QStringLiteral("%1  %2").arg(ip, stateText));
    ipLbl->setStyleSheet(QStringLiteral("font-size: 12px; color: %1;")
                             .arg(GlassTheme::instance().textSecond().name(QColor::HexRgb)));
    bottom->addWidget(ipLbl);
    lay->addLayout(bottom);

    return card;
}

bool DeviceListPage::eventFilter(QObject* watched, QEvent* e) {
    auto* card = qobject_cast<QFrame*>(watched);
    if (card != nullptr && card->property("devCode").isValid()) {
        if (e->type() == QEvent::MouseButtonRelease) {
            emit connectRequested(card->property("devName").toString(),
                                  card->property("devCode").toString(),
                                  card->property("devTrusted").toBool());
        }
    }
    return QWidget::eventFilter(watched, e);
}

// ---------------- 连接码连接页 ----------------

ConnectCodePage::ConnectCodePage(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(28, 24, 28, 24);
    lay->setSpacing(14);

    lay->addWidget(makeTitle(QStringLiteral("通过连接码连接")));
    lay->addWidget(makeHint(QStringLiteral(
        "输入对方首页展示的 9 位连接码，对方确认后建立会话；确认过一次将进入信任缓存")));

    auto* inputRow = new QHBoxLayout();
    inputRow->setSpacing(12);
    inputRow->addStretch();
    for (int i = 0; i < 3; ++i) {
        seg_[i] = new QLineEdit();
        seg_[i]->setFixedSize(96, 48);
        seg_[i]->setAlignment(Qt::AlignCenter);
        seg_[i]->setProperty("placeholderText", "");
        seg_[i]->setPlaceholderText(QStringLiteral("000"));
        seg_[i]->setMaxLength(3);
        QFont f = seg_[i]->font();
        f.setPointSizeF(13.0);
        f.setBold(true);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
        seg_[i]->setFont(f);
        seg_[i]->setValidator(new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("[0-9]{0,3}")), this));
        const int idx = i;
        connect(seg_[i], &QLineEdit::textChanged, this, [this, idx](const QString& t) {
            if (t.length() == 3 && idx < 2) {
                seg_[idx + 1]->setFocus();
                seg_[idx + 1]->selectAll();
            }
        });
        connect(seg_[i], &QLineEdit::returnPressed, this, &ConnectCodePage::tryConnect);
        inputRow->addWidget(seg_[i]);
        if (i < 2) {
            auto* dash = new QLabel(QStringLiteral("–"));
            dash->setStyleSheet(QStringLiteral("color: %1; font-size: 16px;")
                                    .arg(GlassTheme::instance().textHint().name(QColor::HexRgb)));
            inputRow->addWidget(dash);
        }
    }
    inputRow->addSpacing(8);

    auto* goBtn = new QPushButton(QStringLiteral("连  接"));
    goBtn->setObjectName(QStringLiteral("primaryBtn"));
    connect(goBtn, &QPushButton::clicked, this, &ConnectCodePage::tryConnect);
    inputRow->addWidget(goBtn);
    inputRow->addStretch();
    lay->addLayout(inputRow);

    errHint_ = new QLabel(QStringLiteral(" "));
    errHint_->setStyleSheet(QStringLiteral("color: %1;")
                                .arg(GlassTheme::instance().textHint().name(QColor::HexRgb)));
    lay->addWidget(errHint_);

    auto* head = new QHBoxLayout();
    head->addWidget(new QLabel(QStringLiteral("最近连接")));
    head->addStretch();
    head->addWidget(makeHint(QStringLiteral("已信任设备免对方确认")));
    lay->addLayout(head);

    auto* list = new QListWidget();
    list->setCursor(Qt::PointingHandCursor);
    const struct {
        const char* name;
        const char* code;
        const char* time;
    } hist[] = {
        {"设计部-张工", "482 130 556", "今天 14:32"},
        {"会议室-终端", "903 245 117", "昨天 09:15"},
        {"运维室-李工", "107 662 384", "10-19 16:48"},
    };
    for (const auto& h : hist) {
        auto* it = new QListWidgetItem(QString("%1    %2    %3    连接")
                                           .arg(h.name, h.code, h.time));
        it->setData(Qt::UserRole, QString(h.code).remove(' '));
        it->setForeground(GlassTheme::instance().textMain());
        list->addItem(it);
    }
    connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        emit connectRequested(it->data(Qt::UserRole).toString());
    });
    lay->addWidget(list, 1);
}

void ConnectCodePage::tryConnect() {
    QString code;
    for (int i = 0; i < 3; ++i) {
        code += seg_[i]->text();
    }
    if (code.length() != 9) {
        // 错误反馈：提示行染红 + 未填满的段抖动提示（染色 1.6s 后复原）
        errHint_->setText(QStringLiteral("请输满 9 位连接码"));
        errHint_->setStyleSheet(QStringLiteral("color: %1;")
                                    .arg(GlassTheme::instance().accentRed().name(QColor::HexRgb)));
        QTimer::singleShot(1600, this, [this] {
            errHint_->setText(QStringLiteral(" "));
            errHint_->setStyleSheet(QStringLiteral("color: %1;")
                                        .arg(GlassTheme::instance().textHint().name(QColor::HexRgb)));
        });
        return;
    }
    emit connectRequested(code);
}

// ---------------- 远程画面页 ----------------

RemoteViewPage::RemoteViewPage(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    toolbar_ = new QWidget(this);
    toolbar_->setObjectName(QStringLiteral("glassPanel"));
    toolbar_->setFixedSize(240, 44);
    auto* toolLay = new QHBoxLayout(toolbar_);
    toolLay->setContentsMargins(16, 6, 16, 6);
    auto* fullBtn = new QPushButton(QStringLiteral("⛶ 全屏"));
    fullBtn->setObjectName(QStringLiteral("secondaryBtn"));
    auto* discBtn = new QPushButton(QStringLiteral("断开连接"));
    discBtn->setObjectName(QStringLiteral("dangerBtn"));
    toolLay->addWidget(fullBtn);
    toolLay->addWidget(discBtn);
    connect(discBtn, &QPushButton::clicked, this, &RemoteViewPage::disconnected);

    lay->addStretch();
    auto* toolRow = new QHBoxLayout();
    toolRow->addStretch();
    toolRow->addWidget(toolbar_);
    toolRow->addStretch();
    lay->addLayout(toolRow);
    lay->addSpacing(20);
}

void RemoteViewPage::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0));
    p.setPen(QColor(0x66, 0x66, 0x66));
    p.drawText(rect(), Qt::AlignCenter,
               QStringLiteral("远程画面显示区\n（Session Running 后由此渲染 ScreenFrame）"));
}

// ---------------- 文件传输页 ----------------

FileTransferPage::FileTransferPage(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(28, 24, 28, 24);
    lay->setSpacing(16);

    auto* head = new QHBoxLayout();
    head->addWidget(makeTitle(QStringLiteral("文件传输")));
    head->addStretch();
    auto* sendBtn = new QPushButton(QStringLiteral("发送文件…"));
    sendBtn->setObjectName(QStringLiteral("primaryBtn"));
    head->addWidget(sendBtn);
    lay->addLayout(head);

    rows_ = new QVBoxLayout();
    rows_->setSpacing(12);
    rows_->addWidget(makeRow(QStringLiteral("↑"), QStringLiteral("巡检报告-0928.docx"),
                             68, QStringLiteral("3.2 MB/s"),
                             GlassTheme::instance().textSecond()));
    rows_->addWidget(makeRow(QStringLiteral("↓"), QStringLiteral("驱动包_v2.3.zip"),
                             100, QStringLiteral("已完成"),
                             GlassTheme::instance().accentGreen()));
    rows_->addWidget(makeRow(QStringLiteral("↑"), QStringLiteral("拓扑图.png"),
                             24, QStringLiteral("失败"),
                             GlassTheme::instance().accentRed()));
    rows_->addStretch();
    lay->addLayout(rows_, 1);
}

QWidget* FileTransferPage::makeRow(const QString& arrow, const QString& name,
                                   int progress, const QString& status,
                                   const QColor& statusColor) {
    auto* card = new QFrame();
    card->setObjectName(QStringLiteral("deviceCard"));
    card->setFixedHeight(68);
    auto* lay = new QHBoxLayout(card);
    lay->setContentsMargins(14, 12, 14, 12);
    lay->setSpacing(14);

    auto* dirLbl = new QLabel(arrow);
    dirLbl->setStyleSheet(QStringLiteral(
        "font-size: 17px; font-weight: 600; color: %1;")
        .arg((arrow == QStringLiteral("↑")
                  ? GlassTheme::instance().primary()
                  : GlassTheme::instance().accentBlue())
                 .name(QColor::HexRgb)));
    lay->addWidget(dirLbl);

    auto* nameLbl = new QLabel(name);
    nameLbl->setStyleSheet(QStringLiteral("font-size: 13px; color: %1;")
                               .arg(GlassTheme::instance().textMain().name(QColor::HexRgb)));
    lay->addWidget(nameLbl);
    lay->addStretch();

    auto* bar = new QProgressBar();
    bar->setFixedSize(200, 6);
    bar->setTextVisible(false);
    bar->setRange(0, 100);
    bar->setValue(progress);
    if (status == QStringLiteral("失败")) {
        bar->setStyleSheet(QStringLiteral(
            "QProgressBar { background: rgba(110,118,129,0.25); border: none; border-radius: 3px; }"
            "QProgressBar::chunk { background: #F72585; border-radius: 3px; }"));
    }
    lay->addWidget(bar);

    auto* stLbl = new QLabel(status);
    stLbl->setStyleSheet(QStringLiteral("font-size: 12px; color: %1;")
                             .arg(statusColor.name(QColor::HexRgb)));
    lay->addWidget(stLbl);
    return card;
}

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
