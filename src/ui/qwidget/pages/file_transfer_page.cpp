// scToLocalDesk | 文件传输列表页（FileTransferPage）实现（自 pages.cpp 原样拆出）
#include "pages.h"
#include "page_kit.h"

#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include "../glass_theme.h"

namespace sc::localdesk::ui {

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
    rows_->addStretch();
    lay->addLayout(rows_, 1);

    // 空态占位（禁止假数据）：P3 文件传输闭环接入 FileService 事件流
    empty_ = new QLabel(QStringLiteral("暂无传输任务\n连接设备后，从设备卡或\"发送文件\"发起传输"));
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 13px;")
        .arg(GlassTheme::instance().textHint().name(QColor::HexRgb)));
    lay->addWidget(empty_, 1);
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

}  // namespace sc::localdesk::ui
