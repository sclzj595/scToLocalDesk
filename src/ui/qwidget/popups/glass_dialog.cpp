// ============================================================
// scToLocalDesk | src/ui/qwidget/popups/glass_dialog.cpp
// 弹窗唯一门面实现：样式集中（buildQss 玻璃化）+ 行为集中
// （模态/非模态/置顶/闪烁策略只在这里定，调用点零样式代码）
// ============================================================
#include "glass_dialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QObject>

#include "../glass_theme.h"

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>
#pragma comment(lib, "dwmapi.lib")
#endif

namespace sc::localdesk::ui {
namespace GlassDialog {

void warningNonModalTopmost(QWidget* parent, const QString& title,
                            const QString& text) {
    // 堆分配 + WA_DeleteOnClose：非模态 show() 后立即返回，栈对象
    // 会在 lambda 退出时析构导致弹窗闪没（原实现隐患，此处修正）
    auto* box = new QMessageBox(QMessageBox::Warning, title, text,
                                QMessageBox::Ok, parent);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    box->show();  // 非模态：不阻塞主窗口交互
}

void warning(QWidget* parent, const QString& title, const QString& text) {
    QMessageBox::warning(parent, title, text);
}

bool question(QWidget* parent, const QString& title, const QString& text) {
    return QMessageBox::question(parent, title, text, QMessageBox::Yes
                                     | QMessageBox::No,
                                 QMessageBox::Yes)
           == QMessageBox::Yes;
}

QPointer<QMessageBox> confirmIncoming(QWidget* parent, const QString& peerIp,
                                      std::function<void(bool read_only)> onAccept,
                                      std::function<void()> onReject) {
    auto* box = new QMessageBox(parent);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowTitle(QObject::tr("连接请求"));
    box->setText(peerIp.isEmpty()
                     ? QObject::tr("对方请求远程协助本机，是否同意？")
                     : QObject::tr("对方（%1）请求远程协助本机，是否同意？")
                           .arg(peerIp));
    box->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box->setWindowFlag(Qt::WindowStaysOnTopHint, true);  // PRD 确认三件套：置顶
    // 键鼠授权勾选（P1 只读模式）：默认授权；取消勾选 = 只读，
    // approve(read_only) → Approved 附加字节告知控制端显示徽标
    auto* allow_input = new QCheckBox(QObject::tr("允许对方键鼠控制"), box);
    allow_input->setChecked(true);
    box->setCheckBox(allow_input);
    QObject::connect(box, &QMessageBox::finished, box,
                     [onAccept = std::move(onAccept),
                      onReject = std::move(onReject),
                      allow_input](int r) {
                         if (r == QMessageBox::Yes) {
                             onAccept(!allow_input->isChecked());
                         }
                         else {
                             // 含超时自动拒后的收尾 close()：
                             // state 已被看门狗迁移，reject() 幂等
                             onReject();
                         }
                     });
    box->show();
    QApplication::alert(box);  // 任务栏闪烁（被控端可能无人盯屏幕）
    return QPointer<QMessageBox>(box);
}

QString getText(QWidget* parent, const QString& title, const QString& label,
                const QString& initial, bool* ok) {
    bool local_ok = false;
    const QString ret = QInputDialog::getText(parent, title, label,
                                              QLineEdit::Normal, initial,
                                              &local_ok);
    if (ok != nullptr) {
        *ok = local_ok;  // ok 区分"空备注确认"与"取消"
    }
    return ret;
}

QMenu* makeGlassMenu(QWidget* parent) {
    auto* m = new QMenu(parent);
    styleGlassMenu(m);
    return m;
}

void styleGlassMenu(QMenu* m) {
    if (m == nullptr) {
        return;
    }
    // 弹出层形态：不透明窗口 + DWM 原生圆角/描边（Win11 系统路径）。
    // 不用 WA_TranslucentBackground——Qt6.5+ Win11 半透 QMenu 有原生
    // 黑框缺陷（用户截图两次实证），DWM 裁圆+画边则完全绕开 Qt 绘制
    const auto& t = GlassTheme::instance();
    m->setStyleSheet(
        QStringLiteral(
            "QMenu { background: %1; border: none; padding: 6px; }\n"
            "QMenu::item { color: %2; height: 30px; padding: 4px 26px 4px 12px; "
            "border-radius: 6px; margin: 1px 2px; background: transparent; }\n"
            "QMenu::item:selected { background: %3; }\n"
            "QMenu::item:disabled { color: %4; }\n"
            "QMenu::separator { height: 1px; background: %5; margin: 4px 8px; }\n")
            .arg(t.menuBg().name(QColor::HexRgb),
                 t.textMain().name(QColor::HexRgb),
                 t.menuHover().name(QColor::HexRgb),
                 t.textSecond().name(QColor::HexRgb),
                 t.menuDivider().name(QColor::HexRgb)));
#ifdef Q_OS_WIN
    // 每次弹出前设 DWM：圆角（DWMWCP_ROUND）+ 令牌描边
    //（COLORREF 0x00BBGGRR；旧系统不支持时静默降级为方角无描边）
    QObject::connect(m, &QMenu::aboutToShow, m, [m] {
        auto* hwnd = reinterpret_cast<HWND>(m->winId());
        if (hwnd == nullptr) {
            return;
        }
        const auto& c = GlassTheme::instance().menuFrame();
        INT pref = 2;  // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd, 33 /*CORNER_PREFERENCE*/, &pref,
                              sizeof(pref));
        const COLORREF frame = RGB(c.red(), c.green(), c.blue());
        DwmSetWindowAttribute(hwnd, 34 /*BORDER_COLOR*/, &frame,
                              sizeof(frame));
    });
#else
    Q_UNUSED(m)
#endif
}

}  // namespace GlassDialog
}  // namespace sc::localdesk::ui
