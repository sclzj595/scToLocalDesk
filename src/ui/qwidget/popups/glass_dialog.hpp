// ============================================================
// scToLocalDesk | src/ui/qwidget/popups/glass_dialog.hpp
// 弹窗唯一门面（封装化/模块化，Doc13 §3）：系统弹窗禁止在调用点
// 裸调用——警告/问询/入站确认/文本输入/玻璃菜单统一经此入口，
// 令牌样式集中（buildQss 已玻璃化 QMessageBox/QInputDialog；
// 菜单圆角描边走 Win11 DWM 原生路径，绕开半透 QMenu 黑框缺陷）
// ============================================================
#pragma once

#include <QPointer>
#include <QString>

#include <functional>

class QMenu;
class QMessageBox;
class QWidget;

namespace sc::localdesk::ui {

namespace GlassDialog {

/// 非模态置顶警告（端口占用等装配期提示）：不阻塞主窗口交互，
/// 任务栏闪烁提醒（调用方保证在主线程）
void warningNonModalTopmost(QWidget* parent, const QString& title,
                            const QString& text);

/// 模态警告（自连拦截/别名冲突）：exec 阻塞至确认
void warning(QWidget* parent, const QString& title, const QString& text);

/// 模态问询（文件接收确认）：Yes → true
bool question(QWidget* parent, const QString& title, const QString& text);

/// 入站连接确认（PRD 确认三件套：非模态 + 置顶 + 任务栏闪烁）。
/// onAccept/onReject 在用户选择后回调（模态 exec 不可用——30s 超时
/// 信号需能远程关窗，见 Evt_ConnectRequestTimeout）。
/// 返回 QPointer 供超时信号 close()；close 走 finished(No) → onReject
QPointer<QMessageBox> confirmIncoming(QWidget* parent, const QString& peerIp,
                                      std::function<void()> onAccept,
                                      std::function<void()> onReject);

/// 单行文本输入（设备备注等）：ok=false 取消。note 允许空串（ok 区分）
QString getText(QWidget* parent, const QString& title, const QString& label,
                const QString& initial, bool* ok = nullptr);

/// 玻璃菜单工厂：令牌 QSS + Win11 DWM 圆角/描边。
/// 主窗菜单栏 attachMenu 与页面右键菜单共用（菜单形态一致性）
QMenu* makeGlassMenu(QWidget* parent);

/// 对已有 QMenu 施加玻璃样式（令牌 QSS + DWM 圆角/描边 aboutToShow）。
/// 托盘菜单等成员级 QMenu 复用入口——与 makeGlassMenu 同源单点
void styleGlassMenu(QMenu* menu);

}  // namespace GlassDialog

}  // namespace sc::localdesk::ui
