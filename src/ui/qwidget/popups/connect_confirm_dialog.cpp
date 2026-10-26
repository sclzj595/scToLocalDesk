// ============================================================
// scToLocalDesk | src/ui/qwidget/popups/connect_confirm_dialog.cpp
// ============================================================
#include "connect_confirm_dialog.h"

#include <QCheckBox>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QVBoxLayout>

#include "../glass_theme.h"

namespace sc::localdesk::ui {

ConnectConfirmDialog::ConnectConfirmDialog(QWidget* parent) : QDialog(parent) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setModal(true);
    setFixedSize(400, 268);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(24, 20, 24, 20);
    lay->setSpacing(10);

    auto* title = new QLabel(tr("连接请求"));
    title->setObjectName(QStringLiteral("titleLabel"));
    text_ = new QLabel();
    text_->setWordWrap(true);

    // 连接码行（双向核对依据）
    auto* codeCard = new QFrame();
    codeCard->setObjectName(QStringLiteral("codeCard"));
    codeCard->setFixedHeight(44);
    codeCard->setStyleSheet(QStringLiteral(
        "QFrame#codeCard { background: %1; border: 1px solid %2; border-radius: 8px; }")
        .arg(GlassTheme::instance().winBg().name(QColor::HexRgb))
        .arg(QString::fromUtf8("rgba(%1,%2,%3,%4)")
                 .arg(GlassTheme::instance().divider().red())
                 .arg(GlassTheme::instance().divider().green())
                 .arg(GlassTheme::instance().divider().blue())
                 .arg(GlassTheme::instance().divider().alphaF(), 0, 'f', 3)));
    auto* codeLay = new QHBoxLayout(codeCard);
    codeLay->setContentsMargins(12, 0, 12, 0);
    auto* codeHint = new QLabel(tr("对方连接码"));
    codeHint->setObjectName(QStringLiteral("hintLabel"));
    codeLbl_ = new QLabel();
    QFont cf = codeLbl_->font();
    cf.setPointSizeF(11.0);
    cf.setBold(true);
    cf.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
    codeLbl_->setFont(cf);
    codeLbl_->setStyleSheet(QStringLiteral("color: %1;")
                                .arg(GlassTheme::instance().primary().name(QColor::HexRgb)));
    codeLay->addWidget(codeHint);
    codeLay->addStretch();
    codeLay->addWidget(codeLbl_);

    ip_ = new QLabel();
    ip_->setObjectName(QStringLiteral("hintLabel"));

    // 信任记忆复选框（确认后才有记忆缓存，向日葵模式）
    rememberChk_ = new QCheckBox(tr("记住此设备，下次免确认直连"));
    rememberChk_->setChecked(true);

    lay->addWidget(title);
    lay->addWidget(text_);
    lay->addWidget(codeCard);
    lay->addWidget(ip_);
    lay->addWidget(rememberChk_);
    lay->addStretch();

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* rejectBtn = new QPushButton(tr("拒绝"));
    rejectBtn->setObjectName(QStringLiteral("secondaryBtn"));
    auto* acceptBtn = new QPushButton(tr("同意"));
    acceptBtn->setObjectName(QStringLiteral("primaryBtn"));
    btnRow->addWidget(rejectBtn);
    btnRow->addWidget(acceptBtn);
    lay->addLayout(btnRow);

    connect(rejectBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(acceptBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void ConnectConfirmDialog::setPeer(const QString& alias, const QString& ip,
                                   const QString& code) {
    text_->setText(alias + tr(" 请求远程协助本机"));
    codeLbl_->setText(code);
    ip_->setText(QStringLiteral("IP：") + ip);
}

bool ConnectConfirmDialog::rememberChecked() const {
    return rememberChk_->isChecked();
}

void ConnectConfirmDialog::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // 弹窗玻璃卡：12px 圆角 + 半透底 + 1px 描边（红线1/5）
    QPainterPath path;
    path.addRoundedRect(rect(), GlassTheme::kCardRadius, GlassTheme::kCardRadius);
    p.fillPath(path, GlassTheme::instance().glassBg());
    p.setPen(QPen(GlassTheme::instance().glassBorder(), 1));
    p.drawPath(path);
    // 弱阴影：底部 4px 偏移淡带（QSS/自绘近似，Doc13 §1.2）
    QLinearGradient fade(rect().bottomLeft(), rect().bottomLeft() + QPoint(0, 6));
    fade.setColorAt(0, QColor(0, 0, 0, 30));
    fade.setColorAt(1, QColor(0, 0, 0, 0));
    QPainterPath bottom;
    bottom.addRoundedRect(QRectF(2, height() - 6, width() - 4, 6), 4, 4);
    p.fillPath(bottom, fade);
}

void ConnectConfirmDialog::showEvent(QShowEvent* e) {
    QDialog::showEvent(e);
    // 弹出动画：透明度 0→1，180ms（Doc13 §3.4）
    auto* fx = new QGraphicsOpacityEffect(this);
    fx->setOpacity(0);
    setGraphicsEffect(fx);
    auto* anim = new QPropertyAnimation(fx, "opacity", this);
    anim->setDuration(GlassTheme::kDurPopup);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

}  // namespace sc::localdesk::ui
