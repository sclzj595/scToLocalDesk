// scToLocalDesk | 远程画面页（RemoteViewPage）实现（自 pages.cpp 原样拆出）
#include "pages.h"

#include <QButtonGroup>
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
#include "foundation/config_parser.hpp"
#include "foundation/error_text.hpp"
#include "platform/qt_adapter/input_bridge.hpp"
#include "service/app_config.hpp"
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
    toolbar_->setFixedSize(580, 44);  // 加缩放三钮后加宽（原 340）
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
    // 剪贴板同步开关（Doc19 §5 P2）：会话级双向文本同步，默认开；状态
    // 持久化 config session.clipboard_sync（两端同键，关=不发不收）
    clip_btn_ = new QPushButton(QStringLiteral("📋 剪贴板"), toolbar_);
    clip_btn_->setObjectName(QStringLiteral("secondaryBtn"));
    clip_btn_->setFocusPolicy(Qt::NoFocus);  // NoFocus 红线：不吃远端键鼠焦点
    clip_btn_->setCheckable(true);
    clip_btn_->setChecked(true);  // 配置注入前按默认开显示
    connect(clip_btn_, &QPushButton::toggled, this, [this](bool on) {
        if (clip_cfg_ != nullptr) {
            clip_cfg_->clipboard_sync = on;
            if (clip_ini_ != nullptr) {
                clip_cfg_->save(*clip_ini_);
                (void)clip_ini_->save();  // 即时落盘（设置页同口径）
            }
        }
    });
    toolLay->addWidget(clip_btn_);
    // 画面缩放三模式（Doc19 §4.4）：自适应（默认，等比黑边）/拉伸铺满/
    // 1:1 原始像素，互斥 checkable；状态持久化 config view.scale_mode
    //（双 exe 共用键），bind_config 回放，切换即时落盘
    auto* mode_group = new QButtonGroup(this);
    const auto make_mode_btn = [this, mode_group, &toolLay](const char* text) {
        auto* b = new QPushButton(QString::fromUtf8(text), toolbar_);
        b->setObjectName(QStringLiteral("secondaryBtn"));
        b->setFocusPolicy(Qt::NoFocus);  // NoFocus 红线：不吃远端键鼠焦点
        b->setCheckable(true);
        mode_group->addButton(b);  // exclusive 默认开：三钮互斥
        toolLay->addWidget(b);
        return b;
    };
    fit_btn_ = make_mode_btn("自适应");
    stretch_btn_ = make_mode_btn("拉伸");
    pixel_btn_ = make_mode_btn("1:1");
    connect(fit_btn_, &QPushButton::toggled, this, [this](bool on) {
        if (on && scale_mode_ != ScaleMode::Fit) {  // 回放重复置位不重写
            apply_scale_mode(ScaleMode::Fit);
        }
    });
    connect(stretch_btn_, &QPushButton::toggled, this, [this](bool on) {
        if (on && scale_mode_ != ScaleMode::Stretch) {
            apply_scale_mode(ScaleMode::Stretch);
        }
    });
    connect(pixel_btn_, &QPushButton::toggled, this, [this](bool on) {
        if (on && scale_mode_ != ScaleMode::Pixel) {
            apply_scale_mode(ScaleMode::Pixel);
        }
    });
    auto* sep2 = new QLabel(toolbar_);
    sep2->setFixedSize(1, 20);
    sep2->setStyleSheet(sep->styleSheet());
    toolLay->addWidget(sep2);
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
    clamp_pan();  // Pixel 平移偏移随画面尺寸收敛（分辨率切换防出界）
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

void RemoteViewPage::bind_config(foundation::IniConfig* ini, AppConfig* cfg) {
    clip_ini_ = ini;
    clip_cfg_ = cfg;
    if (cfg != nullptr) {
        if (clip_btn_ != nullptr) {
            clip_btn_->setChecked(cfg->clipboard_sync);  // 回放持久化真值
        }
        // 缩放模式回放（Doc19 §4.4）：字符串 → 枚举（非法值 load 已兜底）
        const QString m = QString::fromStdString(cfg->view_scale_mode);
        scale_mode_ = m == QLatin1String("stretch") ? ScaleMode::Stretch
                    : m == QLatin1String("pixel")   ? ScaleMode::Pixel
                                                    : ScaleMode::Fit;
        QPushButton* btn = scale_mode_ == ScaleMode::Stretch ? stretch_btn_
                         : scale_mode_ == ScaleMode::Pixel   ? pixel_btn_
                                                             : fit_btn_;
        if (btn != nullptr) {
            btn->setChecked(true);  // QButtonGroup 互斥：其余自动弹起
        }
    }
}

/// 缩放模式 → config 字符串（view.scale_mode，双 exe 同键）
static const char* scale_mode_str(RemoteViewPage::ScaleMode m) {
    switch (m) {
    case RemoteViewPage::ScaleMode::Stretch:
        return "stretch";
    case RemoteViewPage::ScaleMode::Pixel:
        return "pixel";
    default:
        return "fit";
    }
}

void RemoteViewPage::apply_scale_mode(ScaleMode m) {
    scale_mode_ = m;
    if (m == ScaleMode::Pixel && !frame_.isNull()) {
        // 进入 1:1：居中定位（画面小于视口 clamp 收敛为 0）
        pixel_pan_ = QPointF(qMax(0.0, (frame_.width() - width()) / 2.0),
                             qMax(0.0, (frame_.height() - height()) / 2.0));
    }
    clamp_pan();
    if (clip_cfg_ != nullptr) {
        clip_cfg_->view_scale_mode = scale_mode_str(m);
        if (clip_ini_ != nullptr) {
            clip_cfg_->save(*clip_ini_);
            (void)clip_ini_->save();  // 即时落盘（剪贴板开关同口径）
        }
    }
    update();
}

void RemoteViewPage::clamp_pan() {
    if (frame_.isNull()) {
        pixel_pan_ = QPointF(0, 0);
        return;
    }
    pixel_pan_ = QPointF(
        qBound(0.0, pixel_pan_.x(),
               qMax(0.0, static_cast<double>(frame_.width() - width()))),
        qBound(0.0, pixel_pan_.y(),
               qMax(0.0, static_cast<double>(frame_.height() - height()))));
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
    // 缩放三模式绘制（Doc19 §4.4）：Fit 等比黑边 / Stretch 拉伸铺满 /
    // Pixel 1:1 原始像素（平移偏移取负 = 画面左上角滚出视口的部分不画）
    const QSizeF area = size();
    const QSizeF src = frame_.size();
    p.setRenderHint(QPainter::SmoothPixmapTransform, scale_mode_ != ScaleMode::Pixel);
    if (scale_mode_ == ScaleMode::Stretch) {
        p.drawImage(QRectF(0, 0, area.width(), area.height()), frame_);
        return;
    }
    if (scale_mode_ == ScaleMode::Pixel) {
        p.drawImage(QRectF(-pixel_pan_.x(), -pixel_pan_.y(),
                           src.width(), src.height()), frame_);
        return;
    }
    // 等比缩放铺满（与 QML VideoSurface 同观感，黑边留白）
    const double scale = qMin(area.width() / src.width(),
                              area.height() / src.height());
    const double dw = src.width() * scale;
    const double dh = src.height() * scale;
    p.drawImage(QRectF((area.width() - dw) / 2, (area.height() - dh) / 2, dw, dh),
                frame_);
}

// ---------------- 键鼠透传（Doc18 §4.4，归一化复用 InputBridge） ----------------

void RemoteViewPage::send_mouse(MouseAction action, const QPointF& local, int wheel) {
    if (sessions_ == nullptr) {
        return;
    }
    protocol::MouseKeyBody mk;
    // Pixel 模式无 letterbox：帧像素 = 局部坐标 + 平移偏移（Doc19 §4.4）
    const bool ok = scale_mode_ == ScaleMode::Pixel
        ? platform::InputBridge::map_mouse_pixel(
              static_cast<int>(action), local, pixel_pan_,
              QSizeF(frame_.size()), mk, wheel)
        : platform::InputBridge::map_mouse(
              static_cast<int>(action), local, QSizeF(size()),
              QSizeF(frame_.size()), mk, wheel);
    if (ok) {
        (void)sessions_->send_input_event(mk);
    }
}

void RemoteViewPage::mousePressEvent(QMouseEvent* e) {
    setFocus();  // 点画面即接管键盘
    if (scale_mode_ == ScaleMode::Pixel && e->button() == Qt::MiddleButton) {
        // 1:1 平移（Doc19 §4.4）：中键按压不立即透传——超阈值拖动=平移，
        // 原地松开=中键点击（release 补发 Down+Up，中键功能不丢）
        mid_panning_ = true;
        mid_moved_ = false;
        mid_last_ = e->position();
        return;
    }
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
    if (scale_mode_ == ScaleMode::Pixel && e->button() == Qt::MiddleButton) {
        const bool was_click = !mid_moved_;
        mid_panning_ = false;
        if (was_click) {  // 未拖动 = 中键点击：补发按下+抬起
            send_mouse(MouseAction::MiddleDown, e->position());
            send_mouse(MouseAction::MiddleUp, e->position());
        }
        return;
    }
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
    if (scale_mode_ == ScaleMode::Pixel && mid_panning_
        && (e->buttons() & Qt::MiddleButton) != 0) {
        if (!mid_moved_
            && (e->position() - mid_last_).manhattanLength() > 8) {
            mid_moved_ = true;  // 超阈值进入拖动：松开不再补发中键点击
        }
        if (mid_moved_) {
            pixel_pan_ += mid_last_ - e->position();  // 拖动方向与画面一致
            clamp_pan();
            mid_last_ = e->position();
            update();
            return;  // 平移中不向远端发移动（本地视口操作）
        }
    }
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
