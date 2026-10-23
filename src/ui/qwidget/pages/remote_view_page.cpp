// scToLocalDesk | 远程画面页（RemoteViewPage）实现（自 pages.cpp 原样拆出）
#include "pages.h"

#include <QColor>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace sc::localdesk::ui {

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

void RemoteViewPage::on_frame(const QImage& img) {
    frame_ = img;
    update();  // 触发 paintEvent 重画
}

void RemoteViewPage::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0));
    if (frame_.isNull()) {
        p.setPen(QColor(0x66, 0x66, 0x66));
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("远程画面显示区\n（Session Running 后由此渲染 ScreenFrame）"));
        return;
    }
    // 等比缩放铺满（与 QML VideoSurface 同观感）
    const QSizeF area = size();
    const QSizeF src = frame_.size();
    const double scale = qMin(area.width() / src.width(),
                              area.height() / src.height());
    const double dw = src.width() * scale;
    const double dh = src.height() * scale;
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRectF((area.width() - dw) / 2, (area.height() - dh) / 2, dw, dh),
                frame_);
}

}  // namespace sc::localdesk::ui
