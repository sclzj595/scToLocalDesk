// ============================================================
// scToLocalDesk | src/ui/qwidget/glass_theme.cpp
// QSS 生成：全部选择器一次输出（控件规范 Doc13 §8）
// ============================================================
#include "glass_theme.h"

namespace sc::localdesk::ui {

GlassTheme& GlassTheme::instance() {
    static GlassTheme inst;
    return inst;
}

void GlassTheme::setDark(bool dark) {
    dark_ = dark;
}

namespace {

QString qcolor(const QColor& c) {
    return c.name(QColor::HexRgb);
}

QString qrgba(const QColor& c) {
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(c.alphaF(), 0, 'f', 3);
}

}  // namespace

QString GlassTheme::buildQss() const {
    const QColor winBg = this->winBg();
    const QColor glass = this->glassBg();
    const QColor glassBorder = this->glassBorder();
    const QColor textMain = this->textMain();
    const QColor textSecond = this->textSecond();
    const QColor cardBg = this->cardBg();
    const QColor cardBorder = this->cardBorder();
    const QColor divider = this->divider();
    const QColor hover = this->hoverBg();
    const QColor primary = this->primary();
    const QColor primaryHover = this->primaryHover();
    const QColor primaryDisabled = this->primaryDisabled();

    QString q;
    q += QStringLiteral("QWidget { font-family: 'Microsoft YaHei'; font-size: 13px; color: %1; }\n")
             .arg(qcolor(textMain));
    // 窗口底色（内容区纯色，红线2：无任何模糊/渐变）
    // 右缘两角 20px 内圆角：与 FramelessWindow 自绘圆角对齐，
    // 否则方形白底盖住窗口右下/右上圆角
    q += QStringLiteral("QWidget#contentRoot { background: %1; "
                        "border-top-right-radius: 20px; border-bottom-right-radius: 20px; }\n")
             .arg(qcolor(winBg));
    // 玻璃面板（QSS 模拟磨砂：半透底 + 1px 半透描边 + 圆角）
    q += QStringLiteral("QFrame#glassPanel { background: %1; border: 1px solid %2; border-radius: 0px; }\n")
             .arg(qrgba(glass), qrgba(glassBorder));
    // 设备卡片（hover：主色描边 + 卡面微亮，对齐 QML 三层 hover 的前两层）
    q += QStringLiteral(
             "QFrame#deviceCard { background: %1; border: 1px solid %2; border-radius: 12px; }\n"
             "QFrame#deviceCard:hover { border: 1px solid %3; background: %4; }\n")
             .arg(qcolor(cardBg), qrgba(cardBorder), qcolor(primary), qrgba(hover));
    // 标题栏菜单按钮（QToolButton 自绘菜单：hover 半透高亮，主题自适应）
    q += QStringLiteral(
             "QToolButton#menuBtn { border: none; border-radius: 6px; padding: 5px 12px; "
             "background: transparent; color: %1; }\n"
             "QToolButton#menuBtn:hover { background: %2; }\n"
             "QToolButton#menuBtn::menu-indicator { image: none; }\n")
             .arg(qcolor(textMain), qrgba(hover));
    // 窗口按钮（主题/最小化/最大化：hover 半透；关闭：粉底白字，对齐 QML）
    q += QStringLiteral(
             "QLabel#winBtn { color: %1; border-radius: 6px; background: transparent; }\n"
             "QLabel#winBtn:hover { background: %2; color: %3; }\n"
             "QLabel#winBtnClose { color: %1; border-radius: 6px; background: transparent; }\n"
             "QLabel#winBtnClose:hover { color: #FFFFFF; background: rgba(247,37,133,0.85); }\n")
             .arg(qcolor(textSecond), qrgba(hover), qcolor(textMain));
    // 主按钮
    q += QStringLiteral(
             "QPushButton#primaryBtn { background: %1; color: #FFFFFF; border: none; "
             "border-radius: 8px; padding: 7px 18px; }\n"
             "QPushButton#primaryBtn:hover { background: %2; }\n"
             "QPushButton#primaryBtn:pressed { background: %2; }\n"
             "QPushButton#primaryBtn:disabled { background: %3; color: rgba(255,255,255,0.45); }\n")
             .arg(qcolor(primary), qcolor(primaryHover), qrgba(primaryDisabled));
    // 次要按钮
    q += QStringLiteral(
             "QPushButton#secondaryBtn { background: %1; color: %2; border: 1px solid %3; "
             "border-radius: 8px; padding: 7px 18px; }\n"
             "QPushButton#secondaryBtn:hover { background: %4; }\n")
             .arg(qcolor(cardBg), qcolor(textSecond), qrgba(divider), qrgba(hover));
    // 危险按钮（断开/拒绝 = 红，Doc13 §2.1）
    q += QStringLiteral(
             "QPushButton#dangerBtn { background: %1; color: #FFFFFF; border: none; "
             "border-radius: 8px; padding: 7px 18px; }\n"
             "QPushButton#dangerBtn:hover { background: #F94B9B; }\n")
             .arg(qcolor(this->accentRed()));
    // 输入框
    q += QStringLiteral(
             "QLineEdit { background: %1; color: %2; border: 1px solid %3; "
             "border-radius: 8px; padding: 6px 12px; selection-background-color: %4; }\n"
             "QLineEdit:focus { border: 1px solid %4; }\n")
             .arg(qcolor(winBg), qcolor(textMain), qrgba(divider), qcolor(primary));
    // 列表（设备条目：44px 高、hover 半透底 + 主色描边，对齐 QML 历史行）
    q += QStringLiteral(
             "QListWidget { background: transparent; border: none; outline: none; }\n"
             "QListWidget::item { height: 44px; padding-left: 16px; border-radius: 6px; "
             "border: 1px solid transparent; color: %1; }\n"
             "QListWidget::item:hover { background: %2; border: 1px solid %3; }\n"
             "QListWidget::item:selected { background: %2; border-left: 3px solid %3; }\n")
             .arg(qcolor(textMain), qrgba(hover), qcolor(primary));
    // 进度条（文件传输：6px 细条、主色 chunk、失败行内联覆盖为红）
    q += QStringLiteral(
             "QProgressBar { background: %1; border: none; border-radius: 3px; "
             "min-height: 6px; max-height: 6px; }\n"
             "QProgressBar::chunk { background: %2; border-radius: 3px; }\n")
             .arg(qrgba(divider), qcolor(primary));
    // 复选框 / 开关近似（QSS 无 Switch，用勾选框主色化）
    q += QStringLiteral(
             "QCheckBox { color: %1; spacing: 8px; }\n"
             "QCheckBox::indicator { width: 18px; height: 18px; border-radius: 4px; "
             "border: 1px solid %2; background: %3; }\n"
             "QCheckBox::indicator:checked { background: %4; border: 1px solid %4; }\n")
             .arg(qcolor(textMain), qrgba(divider), qcolor(winBg), qcolor(primary));
    // 滚动条（细条、半透）
    q += QStringLiteral(
             "QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }\n"
             "QScrollBar::handle:vertical { background: %1; border-radius: 4px; min-height: 32px; }\n"
             "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }\n")
             .arg(qrgba(divider));
    // 菜单
    q += QStringLiteral(
             "QMenuBar { background: %1; border-bottom: none; padding-left: 8px; }\n"
             "QMenuBar::item { color: %2; padding: 6px 10px; border-radius: 6px; "
             "margin: 2px 1px; }\n"
             "QMenuBar::item:selected { background: %3; }\n"
             "QMenu { background: %1; border: 1px solid %4; border-radius: 8px; padding: 6px; }\n"
             "QMenu::item { color: %2; height: 28px; padding: 4px 24px 4px 10px; "
             "border-radius: 6px; margin: 1px; }\n"
             "QMenu::item:selected { background: %3; }\n"
             "QMenu::separator { height: 1px; background: %5; margin: 4px 6px; }\n")
             .arg(qrgba(glass), qcolor(textMain), qrgba(hover), qrgba(glassBorder),
                  qrgba(divider));
    // 标签
    q += QStringLiteral("QLabel#titleLabel { font-size: 16px; font-weight: 600; color: %1; }\n"
                        "QLabel#cardTitle { font-size: 14px; font-weight: 600; color: %1; }\n"
                        "QLabel#hintLabel { font-size: 12px; color: %2; }\n")
             .arg(qcolor(textMain), qcolor(textSecond));
    return q;
}

}  // namespace sc::localdesk::ui
