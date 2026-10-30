// scToLocalDesk | 设备发现列表页（DeviceListPage）实现（自 pages.cpp 原样拆出）
// 真数据驱动（Doc18 §3.4）：本机卡 IdService 派生；设备卡 apply_snapshot()
// 与 AliasService 在线表 diff 增量刷新；信任角标走 TrustStore
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
#include <QHostAddress>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include "../glass_theme.h"
#include "../popups/glass_dialog.hpp"
#include "service/alias_service.hpp"
#include "service/id_service.hpp"
#include "service/trust_store.hpp"

namespace sc::localdesk::ui {

namespace {

QString code_text(uint32_t code) {
    return code == 0 ? QStringLiteral("--- --- ---")
                     : QString::fromStdString(IdService::format_code(code));
}

QString code_raw(uint32_t code) {
    return code == 0 ? QString()
                     : QString::number(code).rightJustified(9, '0');
}

}  // namespace

// ---------------- 设备列表页 ----------------

DeviceListPage::DeviceListPage(QWidget* parent) : QWidget(parent) {
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
    self_name_lbl_ = new QLabel(QStringLiteral("—"));
    self_name_lbl_->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 600; color: %1;")
                                      .arg(GlassTheme::instance().textMain().name(QColor::HexRgb)));
    nameCol->addWidget(self_name_lbl_);
    selfLay->addLayout(nameCol);
    selfLay->addStretch();

    auto* codeCol = new QVBoxLayout();
    codeCol->setSpacing(3);
    codeCol->addWidget(makeHint(QStringLiteral("本机连接码")));
    self_code_lbl_ = new QLabel(QStringLiteral("—"));
    QFont codeFont = self_code_lbl_->font();
    codeFont.setPointSizeF(12.0);
    codeFont.setBold(true);
    codeFont.setLetterSpacing(QFont::AbsoluteSpacing, 3.0);
    self_code_lbl_->setFont(codeFont);
    self_code_lbl_->setStyleSheet(QStringLiteral("color: %1;")
                                      .arg(GlassTheme::instance().primary().name(QColor::HexRgb)));
    codeCol->addWidget(self_code_lbl_);
    selfLay->addLayout(codeCol);

    auto* copyBtn = new QPushButton(QStringLiteral("复制"));
    copyBtn->setObjectName(QStringLiteral("secondaryBtn"));
    copyBtn->setStyleSheet(QStringLiteral(
        "color: %1; border: 1px solid rgba(157,78,221,0.45); padding: 6px 16px;")
        .arg(GlassTheme::instance().primary().name(QColor::HexRgb)));
    connect(copyBtn, &QPushButton::clicked, this, [this, copyBtn] {
        QApplication::clipboard()->setText(self_code_lbl_->text().split(' ').join(QString()));
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
    // 手动添加设备（Doc19 §6.3）：跨子网广播不可达时按 IP 直连
    auto* manualBtn = new QPushButton(QStringLiteral("手动添加设备"));
    manualBtn->setObjectName(QStringLiteral("secondaryBtn"));
    manualBtn->setStyleSheet(QStringLiteral(
        "color: %1; border: 1px solid rgba(157,78,221,0.45); padding: 6px 16px;")
        .arg(GlassTheme::instance().primary().name(QColor::HexRgb)));
    connect(manualBtn, &QPushButton::clicked, this,
            &DeviceListPage::on_manual_add);
    head->addWidget(manualBtn);
    lay->addLayout(head);

    // ---- 设备卡网格（演示占位移除，apply_snapshot 增量填充） ----
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
    grid_->setColumnStretch(0, 1);
    grid_->setColumnStretch(1, 1);
    grid_->setColumnStretch(2, 1);
    grid_->setRowStretch(1, 1);
}

void DeviceListPage::bind_services(AliasService* aliases, TrustStore* trust) {
    aliases_ = aliases;
    trust_ = trust;
}

void DeviceListPage::set_self(const QString& name, const QString& code) {
    self_name_lbl_->setText(name.isEmpty() ? QStringLiteral("—") : name);
    self_code_lbl_->setText(code.isEmpty() ? QStringLiteral("—") : code);
}

void DeviceListPage::apply_snapshot() {
    if (aliases_ == nullptr) {
        return;
    }
    const auto devices = aliases_->online_devices();
    std::map<std::string, bool> seen;
    for (const auto& dev : devices) {
        seen[dev.alias] = true;
        const auto it = cards_.find(dev.alias);
        const bool trusted = trust_ != nullptr && dev.code != 0
                             && trust_->is_trusted(code_raw(dev.code).toStdString());
        if (it == cards_.end()) {
            // 新上线：建卡入网格（手动对端带「手动」小标，Doc19 §6.3）
            Card c;
            c.widget = makeDeviceCard(QString::fromStdString(dev.alias),
                                      code_text(dev.code),
                                      QString::fromStdString(dev.ip)
                                          + (dev.manual ? QStringLiteral(" · 手动")
                                                        : QString()),
                                      dev.status == 0x01, trusted);
            c.code = dev.code;
            c.busy = dev.status == 0x01;
            c.trusted = trusted;
            c.ip = dev.ip;
            cards_.emplace(dev.alias, c);
            reflow();
        }
        else {
            // 已在线：仅变化项更新卡面（增量，不重建）
            Card& c = it->second;
            bool dirty = false;
            if (static_cast<bool>(dev.status == 0x01) != c.busy) {
                c.busy = dev.status == 0x01;
                dirty = true;
            }
            if (dev.code != 0 && dev.code != c.code) {
                c.code = dev.code;
                dirty = true;
            }
            if (trusted != c.trusted) {
                c.trusted = trusted;
                dirty = true;
            }
            if (dev.ip != c.ip) {
                c.ip = dev.ip;
                dirty = true;
            }
            if (dirty) {
                c.widget->setProperty("devCode", code_raw(c.code));
                c.widget->setProperty("devTrusted", c.trusted);
                auto* card = qobject_cast<QFrame*>(c.widget);
                if (card != nullptr) {
                    card->findChild<QLabel*>(QStringLiteral("codeLbl"))
                        ->setText(code_text(c.code));
                    card->findChild<QLabel*>(QStringLiteral("ipLbl"))
                        ->setText(c.busy ? QStringLiteral("会话中")
                                         : QStringLiteral("可连接"));
                    auto* dot = card->findChild<QLabel*>(QStringLiteral("dotLbl"));
                    if (dot != nullptr) {
                        dot->setStyleSheet(QStringLiteral(
                            "background: %1; border-radius: 4px; max-width: 8px; max-height: 8px;")
                            .arg(c.busy ? GlassTheme::instance().accentGreen().name(QColor::HexRgb)
                                        : GlassTheme::instance().accentBlue().name(QColor::HexRgb)));
                    }
                    auto* badge = card->findChild<QLabel*>(QStringLiteral("badgeLbl"));
                    if (badge != nullptr) {
                        badge->setText(c.trusted ? QStringLiteral("已信任")
                                                 : QStringLiteral("待确认"));
                        badge->setStyleSheet(QStringLiteral(
                            "font-size: 11px; padding: 3px 8px; border-radius: 4px; color: %1; "
                            "background: %2;")
                            .arg(c.trusted ? GlassTheme::instance().accentGreen().name(QColor::HexRgb)
                                           : GlassTheme::instance().textHint().name(QColor::HexRgb))
                            .arg(c.trusted ? "rgba(76,201,240,0.12)" : "rgba(110,118,129,0.10)"));
                    }
                }
            }
        }
    }
    // 离线：快照中不存在的卡移除
    for (auto it = cards_.begin(); it != cards_.end();) {
        if (seen.find(it->first) == seen.end()) {
            it->second.widget->deleteLater();
            it = cards_.erase(it);
            reflow();
        }
        else {
            ++it;
        }
    }
}

QString DeviceListPage::deviceIp(const QString& code) const {
    bool ok = false;
    const uint32_t c = code.toUInt(&ok);
    if (!ok || c == 0) {
        return {};
    }
    for (const auto& [alias, card] : cards_) {
        if (card.code == c) {
            return QString::fromStdString(card.ip);
        }
    }
    return {};
}

// 「手动添加设备」（Doc19 §6.3）：GlassDialog 两次收集别名 + IP[:端口]，
// 校验通过交装配层回调（bridge.addManualPeer：持久化 + 立即注入探活）
void DeviceListPage::on_manual_add() {
    if (manual_add_ == nullptr) {
        GlassDialog::warning(this, QStringLiteral("手动添加设备"),
                             QStringLiteral("配置未注入，暂不可用"));
        return;
    }
    const QString alias = GlassDialog::getText(
        this, QStringLiteral("手动添加设备"),
        QStringLiteral("连接名（3~20 字符，不含 ; | =）"), {});
    if (alias.isEmpty()) {
        return;  // 取消
    }
    const QString ipport = GlassDialog::getText(
        this, QStringLiteral("手动添加设备"),
        QStringLiteral("对端 IP[:端口]（端口留空用默认会话端口）"), {});
    if (ipport.isEmpty()) {
        return;
    }
    // ip[:port] 拆分（IPv4 无冒号，最后一个 ':' 后为端口）
    const int colon = ipport.lastIndexOf(QLatin1Char(':'));
    const QString ip = (colon < 0 ? ipport : ipport.left(colon)).trimmed();
    const QString port_s = (colon < 0 ? QString() : ipport.mid(colon + 1)).trimmed();
    uint16_t port = 0;  // 0 = 桥侧取默认 WS 端口
    if (!port_s.isEmpty()) {
        bool ok = false;
        const int p = port_s.toInt(&ok);
        if (!ok || p < 1024 || p > 65535) {
            GlassDialog::warning(this, QStringLiteral("手动添加设备"),
                                 QStringLiteral("端口需 1024~65535"));
            return;
        }
        port = static_cast<uint16_t>(p);
    }
    QHostAddress addr;
    if (addr.setAddress(ip) == false
        || addr.protocol() != QAbstractSocket::IPv4Protocol) {
        GlassDialog::warning(this, QStringLiteral("手动添加设备"),
                             QStringLiteral("IP 格式非法（仅支持 IPv4）"));
        return;
    }
    if (!manual_add_(alias.trimmed(), ip, port)) {
        GlassDialog::warning(this, QStringLiteral("手动添加设备"),
                             QStringLiteral("添加失败：连接名需 3~20 字符且不含 ; | =，"
                                             "或与已有条目冲突"));
    }
}

QString DeviceListPage::deviceAlias(const QString& code) const {
    bool ok = false;
    const uint32_t c = code.toUInt(&ok);
    if (!ok || c == 0) {
        return {};
    }
    for (const auto& [alias, card] : cards_) {
        if (card.code == c) {
            return QString::fromStdString(alias);  // cards_ key 即别名
        }
    }
    return {};
}

void DeviceListPage::reflow() {
    int i = 0;
    for (auto& kv : cards_) {
        grid_->addWidget(kv.second.widget, i / 3, i % 3);
        ++i;
    }
}

QWidget* DeviceListPage::makeDeviceCard(const QString& name, const QString& code,
                                        const QString& ip, bool busy, bool trusted) {
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
    auto* ipTop = new QLabel(ip);
    ipTop->setObjectName(QStringLiteral("ipTopLbl"));
    ipTop->setStyleSheet(QStringLiteral("font-size: 12px; color: %1;")
                             .arg(GlassTheme::instance().textHint().name(QColor::HexRgb)));
    nameCol->addWidget(ipTop);
    top->addLayout(nameCol);
    top->addStretch();

    // 信任角标（向日葵模式：确认后才有记忆缓存）
    auto* badge = new QLabel(trusted ? QStringLiteral("已信任") : QStringLiteral("待确认"));
    badge->setObjectName(QStringLiteral("badgeLbl"));
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
    codeLbl->setObjectName(QStringLiteral("codeLbl"));
    QFont cf = codeLbl->font();
    cf.setBold(true);
    cf.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    codeLbl->setFont(cf);
    codeLbl->setStyleSheet(QStringLiteral("font-size: 13px; color: %1;")
                               .arg(GlassTheme::instance().primary().name(QColor::HexRgb)));
    bottom->addWidget(codeLbl);
    bottom->addStretch();
    auto* dot = new QLabel();
    dot->setObjectName(QStringLiteral("dotLbl"));
    dot->setStyleSheet(QStringLiteral(
        "background: %1; border-radius: 4px; max-width: 8px; max-height: 8px;")
        .arg(busy ? GlassTheme::instance().accentGreen().name(QColor::HexRgb)
                  : GlassTheme::instance().accentBlue().name(QColor::HexRgb)));
    bottom->addWidget(dot);
    // 状态行对齐 QML：仅状态文字不含 IP（IP 已在连接名下方展示，避免重复）
    auto* ipLbl = new QLabel(busy ? QStringLiteral("会话中")
                                  : QStringLiteral("可连接"));
    ipLbl->setObjectName(QStringLiteral("ipLbl"));
    ipLbl->setStyleSheet(QStringLiteral("font-size: 12px; color: %1;")
                             .arg(GlassTheme::instance().textSecond().name(QColor::HexRgb)));
    bottom->addWidget(ipLbl);
    lay->addLayout(bottom);

    return card;
}

bool DeviceListPage::eventFilter(QObject* watched, QEvent* e) {
    auto* card = qobject_cast<QFrame*>(watched);
    if (card != nullptr && card->property("devCode").isValid()) {
        if (e->type() == QEvent::MouseButtonPress) {
            // 消费按下：抓取留在卡片上，释放必达——否则 press 冒泡后隐式
            // 抓取漂移，鼠标微抖即丢点击（与主窗口标题栏按钮同机理）
            return true;
        }
        if (e->type() == QEvent::MouseButtonRelease) {
            // 无对端连接码（纯静态条目）也可点（对齐 QML）：连接走 alias 寻址
            const QString code = card->property("devCode").toString();
            emit connectRequested(card->property("devName").toString(),
                                  code,
                                  card->property("devTrusted").toBool());
        }
    }
    return QWidget::eventFilter(watched, e);
}

}  // namespace sc::localdesk::ui
