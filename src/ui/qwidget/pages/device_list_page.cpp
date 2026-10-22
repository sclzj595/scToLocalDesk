// scToLocalDesk | 设备发现列表页（DeviceListPage）实现（自 pages.cpp 原样拆出）
#include "pages.h"
#include "page_kit.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include "../glass_theme.h"

namespace sc::localdesk::ui {

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

}  // namespace sc::localdesk::ui
