// scToLocalDesk | 连接码连接页（ConnectCodePage）实现（自 pages.cpp 原样拆出）
#include "pages.h"
#include "page_kit.h"

#include <QColor>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QTimer>
#include <QVBoxLayout>

#include "../glass_theme.h"

namespace sc::localdesk::ui {

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

}  // namespace sc::localdesk::ui
