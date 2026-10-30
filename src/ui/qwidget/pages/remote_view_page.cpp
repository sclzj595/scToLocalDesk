// scToLocalDesk | 远程画面页（RemoteViewPage）实现（自 pages.cpp 原样拆出）
#include "pages.h"

#include <QColor>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "../glass_theme.h"
#include "core/interface/iinput_injector.hpp"
#include "core/interface/types.hpp"
#include "foundation/error_text.hpp"
#include "platform/qt_adapter/input_bridge.hpp"
#include "service/remote_session_service.hpp"

namespace sc::localdesk::ui {

// ---------------- 远程画面页 ----------------

RemoteViewPage::RemoteViewPage(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);          // 未按下移动也跟踪（远程光标连续移动）
    setFocusPolicy(Qt::StrongFocus); // 键盘事件前提（点画面 setFocus 接管）
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    toolbar_ = new QWidget(this);
    toolbar_->setObjectName(QStringLiteral("glassPanel"));
    toolbar_->setFixedSize(240, 44);
    auto* toolLay = new QHBoxLayout(toolbar_);
    toolLay->setContentsMargins(16, 6, 16, 6);
    toolLay->setSpacing(8);
    auto* fullBtn = new QPushButton(QStringLiteral("⛶ 全屏"));
    fullBtn->setObjectName(QStringLiteral("secondaryBtn"));
    // 对齐 QML fullBtn：全屏/还原切换（原为未接 clicked 的死按钮）
    connect(fullBtn, &QPushButton::clicked, this, [this] {
        QWidget* w = window();
        if (w->isFullScreen()) {
            w->showNormal();
        }
        else {
            w->showFullScreen();
        }
    });
    auto* discBtn = new QPushButton(QStringLiteral("断开连接"));
    discBtn->setObjectName(QStringLiteral("dangerBtn"));
    // NoFocus 红线：按钮不吃键盘焦点——点击后 Space/Enter 激活按钮、
    // Tab 走焦点导航，远端收不到这些键（与 QML 版 Page_RemoteView 同修）
    fullBtn->setFocusPolicy(Qt::NoFocus);
    discBtn->setFocusPolicy(Qt::NoFocus);
    toolLay->addWidget(fullBtn);
    // 分隔线：对齐 QML 工具条两钮间的 1px divider
    auto* sep = new QLabel(toolbar_);
    sep->setFixedSize(1, 20);
    sep->setStyleSheet(QStringLiteral(
        "background: %1;")
        .arg(GlassTheme::instance().divider().name(QColor::HexRgb)));
    toolLay->addWidget(sep);
    toolLay->addWidget(discBtn);
    connect(discBtn, &QPushButton::clicked, this, &RemoteViewPage::disconnected);
    toolbar_->hide();  // 无会话默认隐藏（断开按钮不可用），on_frame 恢复

    // 只读徽标（P1）：顶部悬浮小胶囊——控制端收到 Approved 附加字节=1
    // （对端未授权键鼠）时显示，仅告知；本地输入照常采集发送，由被控端丢弃
    ro_badge_ = new QLabel(QStringLiteral("只读模式（对方未授权键鼠）"), this);
    ro_badge_->setObjectName(QStringLiteral("roBadge"));
    ro_badge_->setAlignment(Qt::AlignCenter);
    ro_badge_->setStyleSheet(QStringLiteral(
        "background: %1; color: white; border-radius: 10px; "
        "font-size: 12px; padding: 4px 14px;")
        .arg(GlassTheme::instance().textSecond().name(QColor::HexRgb)));
    ro_badge_->adjustSize();
    ro_badge_->move((width() - ro_badge_->width()) / 2, 12);
    ro_badge_->hide();

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
    if (placeholder_) {  // 正常帧到达：复位占位状态
        placeholder_ = false;
        placeholder_locked_ = false;
    }
    toolbar_->setVisible(true);  // 有帧 = 会话中，恢复工具条
    update();  // 触发 paintEvent 重画
}

void RemoteViewPage::on_placeholder(bool locked) {
    placeholder_ = true;
    placeholder_locked_ = locked;
    update();
}

void RemoteViewPage::on_session_started(const QString& alias) {
    session_active_ = true;
    peer_ = alias;
    is_controller_ = sessions_ == nullptr || sessions_->is_controller();
    toolbar_->setVisible(true);  // 无帧会话也恢复（断开必须可用，Doc16 §2.6）
    update();
}

void RemoteViewPage::on_read_only(bool on) {
    if (ro_badge_ != nullptr) {
        ro_badge_->setVisible(on);  // 只读徽标（会话关闭在 on_session_closed 复位）
    }
}

void RemoteViewPage::on_session_closed() {
    frame_ = QImage();
    placeholder_ = false;
    placeholder_locked_ = false;
    session_active_ = false;
    toolbar_->hide();
    if (ro_badge_ != nullptr) {
        ro_badge_->hide();  // 只读徽标随会话复位
    }
    update();
}

void RemoteViewPage::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0));
    if (placeholder_) {
        // 采集失败占位（Doc19 §4.2）：灰底 + 居中提示（titleLabel 体系字：
        // 16px 半粗），会话保持不断、键鼠透传不受影响。
        // 文案走 foundation 映射（员工视图红线：内部错误码不出 UI）
        p.fillRect(rect(), QColor(0xEC, 0xEF, 0xF3));
        QFont f = p.font();
        f.setPixelSize(16);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(QColor(0x66, 0x66, 0x66));
        const auto cap_err = placeholder_locked_
            ? static_cast<int>(CaptureErr::DesktopLocked)
            : static_cast<int>(CaptureErr::DriverError);
        p.drawText(rect(), Qt::AlignCenter,
                   QString::fromUtf8(foundation::friendly_capture_fail(cap_err)));
        return;
    }
    if (frame_.isNull()) {
        p.setPen(QColor(0x66, 0x66, 0x66));
        // 空态三分支（与 QML Page_RemoteView 同口径）：无会话占位 /
        // 控制端等画面 / 被控端提示被控中（会话活着但无帧 ≠ 无会话）
        if (session_active_) {
            if (is_controller_) {
                p.drawText(rect(), Qt::AlignCenter,
                           QStringLiteral("已连接 %1，等待对方画面…\n（对方未开启画面共享或对方暂无采集能力）")
                               .arg(peer_));
            }
            else {
                p.drawText(rect(), Qt::AlignCenter,
                           QStringLiteral("正在被 %1 远程控制").arg(peer_));
            }
            return;
        }
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("暂未进行远程控制\n从设备列表或连接码页发起会话后，画面将显示在此处"));
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

// ---------------- 键鼠透传（Doc18 §4.4，归一化复用 InputBridge） ----------------

void RemoteViewPage::send_mouse(MouseAction action, const QPointF& local, int wheel) {
    if (sessions_ == nullptr) {
        return;
    }
    protocol::MouseKeyBody mk;
    if (platform::InputBridge::map_mouse(
            static_cast<int>(action), local, QSizeF(size()),
            QSizeF(frame_.size()), mk, wheel)) {
        (void)sessions_->send_input_event(mk);
    }
}

void RemoteViewPage::mousePressEvent(QMouseEvent* e) {
    setFocus();  // 点画面即接管键盘
    switch (e->button()) {
    case Qt::LeftButton:
        send_mouse(MouseAction::LeftDown, e->position());
        break;
    case Qt::RightButton:
        send_mouse(MouseAction::RightDown, e->position());
        break;
    case Qt::MiddleButton:
        send_mouse(MouseAction::MiddleDown, e->position());
        break;
    default:
        break;
    }
}

void RemoteViewPage::mouseReleaseEvent(QMouseEvent* e) {
    switch (e->button()) {
    case Qt::LeftButton:
        send_mouse(MouseAction::LeftUp, e->position());
        break;
    case Qt::RightButton:
        send_mouse(MouseAction::RightUp, e->position());
        break;
    case Qt::MiddleButton:
        send_mouse(MouseAction::MiddleUp, e->position());
        break;
    default:
        break;
    }
}

void RemoteViewPage::mouseMoveEvent(QMouseEvent* e) {
    send_mouse(MouseAction::Move, e->position());
}

void RemoteViewPage::wheelEvent(QWheelEvent* e) {
    if (e->angleDelta().y() != 0) {
        send_mouse(MouseAction::Wheel, e->position(), e->angleDelta().y());
    }
}

void RemoteViewPage::keyPressEvent(QKeyEvent* e) {
    if (sessions_ != nullptr) {
        protocol::MouseKeyBody mk;
        if (platform::InputBridge::map_key(e->key(), true, mk)) {
            (void)sessions_->send_input_event(mk);
        }
    }
}

void RemoteViewPage::keyReleaseEvent(QKeyEvent* e) {
    if (sessions_ != nullptr) {
        protocol::MouseKeyBody mk;
        if (platform::InputBridge::map_key(e->key(), false, mk)) {
            (void)sessions_->send_input_event(mk);
        }
    }
}

}  // namespace sc::localdesk::ui
