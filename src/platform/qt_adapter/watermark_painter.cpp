// ============================================================
// scToLocalDesk | src/platform/qt_adapter/watermark_painter.cpp
// ============================================================
#include "platform/qt_adapter/watermark_painter.hpp"

#include <QDateTime>
#include <QFont>
#include <QPainter>

namespace sc::localdesk::platform {

QImage render_session_watermark(const QString& peer_alias) {
    if (peer_alias.isEmpty()) {
        return {};
    }
    const QString text = peer_alias + QStringLiteral(" · ")
                         + QDateTime::currentDateTime().toString(
                             QStringLiteral("yyyy-MM-dd HH:mm"));
    QFont font(QStringLiteral("Microsoft YaHei"));
    font.setPixelSize(20);
    font.setBold(true);

    // 两遍渲染：先量尺寸再实画（含文字描边留白）
    QFontMetrics fm(font);
    const int pad = 12;
    const int w = fm.horizontalAdvance(text) + pad * 2;
    const int h = fm.height() + pad * 2;

    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setFont(font);
    p.setRenderHint(QPainter::Antialiasing);
    // 深色描边 + 白字：亮暗桌面背景均可辨（取色无关）
    QPen pen(QColor(0, 0, 0, 140), 3);
    p.setPen(pen);
    p.drawText(img.rect().adjusted(pad, pad, -pad, -pad),
               Qt::AlignCenter, text);
    p.setPen(Qt::white);
    p.drawText(img.rect().adjusted(pad, pad, -pad, -pad),
               Qt::AlignCenter, text);
    p.end();
    return img;
}

}  // namespace sc::localdesk::platform
