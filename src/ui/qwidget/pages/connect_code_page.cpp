// scToLocalDesk | 连接码连接页（ConnectCodePage）实现（自 pages.cpp 原样拆出）
#include "pages.h"
#include "page_kit.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDate>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QTimer>
#include <QVBoxLayout>

#include "../glass_theme.h"
#include "service/trust_store.hpp"

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
        seg_[idx]->installEventFilter(this);
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

    hist_list_ = new QListWidget();
    hist_list_->setCursor(Qt::PointingHandCursor);
    connect(hist_list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        emit connectRequested(it->data(Qt::UserRole).toString());
    });
    lay->addWidget(hist_list_, 1);

    // 空态占位（禁止假数据）：无信任记录时的提示行
    hist_empty_ = new QLabel(QStringLiteral("暂无连接记录\n首次连接经对方确认后，将显示在此处"));
    hist_empty_->setAlignment(Qt::AlignCenter);
    hist_empty_->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 13px;")
        .arg(GlassTheme::instance().textHint().name(QColor::HexRgb)));
    lay->addWidget(hist_empty_, 1);
    hist_empty_->hide();
}

void ConnectCodePage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    reload_history();  // 信任变更发生在确认弹窗，切页回来即最新
}

void ConnectCodePage::reload_history() {
    if (hist_list_ == nullptr) {
        return;
    }
    hist_list_->clear();
    // trusted_at "2026-10-25 14:32:05" → 相对时间文案（与 QML 版同规则）
    const QDate today = QDate::currentDate();
    if (trust_ != nullptr) {
        for (const auto& e : trust_->list()) {
            const QDateTime t = QDateTime::fromString(
                QString::fromStdString(e.trusted_at), "yyyy-MM-dd HH:mm:ss");
            QString time_text;
            if (t.date() == today) {
                time_text = t.toString("'今天' HH:mm");
            } else if (t.date() == today.addDays(-1)) {
                time_text = t.toString("'昨天' HH:mm");
            } else if (t.date().year() == today.year()) {
                time_text = t.toString("MM-dd HH:mm");
            } else {
                time_text = t.toString("yyyy-MM-dd");
            }
            // 9 位裸码 → "xxx xxx xxx" 三段展示
            const QString raw = QString::fromStdString(e.code);
            QString grouped;
            for (int i = 0; i < raw.size(); ++i) {
                if (i > 0 && i % 3 == 0) {
                    grouped += QLatin1Char(' ');
                }
                grouped += raw.at(i);
            }
            auto* it = new QListWidgetItem(
                QStringLiteral("%1    %2    %3    连接")
                    .arg(QString::fromStdString(e.name), grouped, time_text));
            it->setData(Qt::UserRole, raw);
            it->setForeground(GlassTheme::instance().textMain());
            hist_list_->addItem(it);
        }
    }
    const bool empty = hist_list_->count() == 0;
    hist_list_->setVisible(!empty);
    hist_empty_->setVisible(empty);
}

bool ConnectCodePage::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        for (int i = 0; i < 3; ++i) {
            if (watched == seg_[i]) {
                return handleSegKey(i, static_cast<QKeyEvent*>(event));
            }
        }
    }
    else if (event->type() == QEvent::FocusIn) {
        // 点击/Tab 进入选中态，直接覆盖输入（与 QML 版一致）
        if (auto* ed = qobject_cast<QLineEdit*>(watched)) {
            ed->selectAll();
        }
    }
    return QWidget::eventFilter(watched, event);
}

bool ConnectCodePage::handleSegKey(int idx, QKeyEvent* ev) {
    auto* ed = seg_[idx];
    // 满格续输：数字直接落下一格（不依赖 textChanged 跳焦时序）
    if (ev->key() >= Qt::Key_0 && ev->key() <= Qt::Key_9 && ed->text().length() == 3
        && ed->selectedText().isEmpty() && idx < 2) {
        auto* nxt = seg_[idx + 1];
        nxt->setFocus();
        nxt->setCursorPosition(nxt->text().length());
        QString t = nxt->text() + ev->text();
        nxt->setText(t.right(3));
        return true;
    }
    // 粘贴拦截：2 位以上走三段分发（复制整码/半码均优雅落地）
    if (ev->matches(QKeySequence::Paste)) {
        QString only;
        for (const QChar c : QApplication::clipboard()->text()) {
            if (c.isDigit()) {
                only += c;
            }
        }
        if (only.length() >= 2) {
            distributePaste(idx, only);
            return true;
        }
        return false;  // 单个数字走默认粘贴
    }
    if (ev->key() == Qt::Key_Backspace) {
        if (ed->text().isEmpty() && idx > 0) {
            seg_[idx - 1]->setFocus();
            seg_[idx - 1]->selectAll();
            return true;
        }
        // 光标在本段 0 位无选区：删上一段末位（三段视作连续串）
        if (!ed->text().isEmpty() && ed->cursorPosition() == 0
            && ed->selectedText().isEmpty() && idx > 0) {
            auto* p = seg_[idx - 1];
            QString t = p->text();
            t.chop(1);
            p->setText(t);
            p->setFocus();
            p->setCursorPosition(t.length());
            return true;
        }
        return false;
    }
    if (ev->key() == Qt::Key_Left && ed->cursorPosition() == 0 && idx > 0) {
        seg_[idx - 1]->setFocus();
        seg_[idx - 1]->setCursorPosition(seg_[idx - 1]->text().length());
        return true;
    }
    if (ev->key() == Qt::Key_Right && ed->cursorPosition() == ed->text().length()
        && ed->text().length() == 3 && idx < 2) {
        seg_[idx + 1]->setFocus();
        seg_[idx + 1]->setCursorPosition(0);
        return true;
    }
    return false;
}

void ConnectCodePage::distributePaste(int idx, const QString& digits) {
    const QString all = seg_[0]->text() + seg_[1]->text() + seg_[2]->text();
    const int pos = idx * 3;
    QString merged = all.left(pos) + digits + all.mid(pos + seg_[idx]->text().length());
    QString only;
    for (const QChar c : merged) {
        if (c.isDigit()) {
            only += c;
        }
    }
    merged = only.left(9);
    seg_[0]->setText(merged.mid(0, 3));
    seg_[1]->setText(merged.mid(3, 3));
    seg_[2]->setText(merged.mid(6, 3));
    const int filled = seg_[0]->text().length() + seg_[1]->text().length()
                     + seg_[2]->text().length();
    auto* target = filled <= 3 ? seg_[0] : filled <= 6 ? seg_[1] : seg_[2];
    target->setFocus();
    target->setCursorPosition(target->text().length());
    if (filled == 9) {
        tryConnect();
    }
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
