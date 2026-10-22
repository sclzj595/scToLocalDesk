// ============================================================
// scToLocalDesk | src/ui/qwidget/pages/page_kit.h
// 页面通用 widget 工厂（原 pages.cpp 匿名命名空间函数，inline 化）：
// makeTitle / makeHint / makeAvatar / makeStateDot
// ============================================================
#pragma once

#include <QColor>
#include <QLabel>
#include <QString>

namespace sc::localdesk::ui {

inline QLabel* makeTitle(const QString& text) {
    auto* lbl = new QLabel(text);
    lbl->setObjectName(QStringLiteral("titleLabel"));
    return lbl;
}

inline QLabel* makeHint(const QString& text) {
    auto* lbl = new QLabel(text);
    lbl->setObjectName(QStringLiteral("hintLabel"));
    return lbl;
}

/// 头像圆标（淡紫底 + 主色首字）
inline QLabel* makeAvatar(const QString& ch, int size) {
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
inline QLabel* makeStateDot(const QColor& c) {
    auto* dot = new QLabel(QStringLiteral("●"));
    dot->setStyleSheet(QStringLiteral("color: %1; font-size: 9px;")
                           .arg(c.name(QColor::HexRgb)));
    return dot;
}

}  // namespace sc::localdesk::ui
