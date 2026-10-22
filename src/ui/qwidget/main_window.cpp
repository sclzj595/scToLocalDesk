// ============================================================
// scToLocalDesk | src/ui/qwidget/main_window.cpp
// 布局：32px 玻璃标题栏（可拖拽）+ 72px 玻璃 Dock + 纯色内容栈
// ============================================================
#include "main_window.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "glass_theme.h"
#include "pages/pages.h"
#include "popups/connect_confirm_dialog.h"
#include "service/trust_store.hpp"

namespace sc::localdesk::ui {

namespace {
constexpr int kEntryPages = 5;  // Dock 条目数
}  // namespace

MainWindow::MainWindow() {
    resize(1080, 700);
    setMinimumSize(860, 560);

    auto* root = new QVBoxLayout();
    root->setContentsMargins(1, 1, 1, 1);  // 让位圆角描边
    root->setSpacing(0);

    // ---------------- 顶层：玻璃标题栏（红线4：即窗口标题栏） ----------------
    root->addWidget(makeTitleBar());

    // ---------------- 中层：Dock + 内容栈 ----------------
    auto* body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(makeDock());

    stack_ = new QStackedWidget();
    stack_->setObjectName(QStringLiteral("contentRoot"));
    auto* devList = new DeviceListPage();
    auto* connCode = new ConnectCodePage();
    auto* settingsPage = new SettingsPage();
    stack_->addWidget(devList);                    // 0 设备列表
    stack_->addWidget(connCode);                   // 1 连接码连接
    stack_->addWidget(new RemoteViewPage());       // 2 远程画面
    stack_->addWidget(new FileTransferPage());     // 3 文件传输
    stack_->addWidget(settingsPage);               // 4 设置
    // 设置页：深色开关 → 统一主题入口；清空信任缓存 → TrustStore
    connect(settingsPage, &SettingsPage::themeChanged, this, &MainWindow::applyTheme);
    connect(settingsPage, &SettingsPage::clearTrustRequested, this, [this] {
        if (trust_ != nullptr) {
            trust_->clear();
        }
    });
    updateDockStyle();   // 栈已就绪：应用初始选中态（第 0 项竖线+染色）
    body->addWidget(stack_, 1);
    root->addLayout(body, 1);

    // ---------------- 底部 Toast 行（向日葵模式反馈） ----------------
    toastLbl_ = new QLabel();
    toastLbl_->setAlignment(Qt::AlignCenter);
    toastLbl_->setFixedHeight(30);
    toastLbl_->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 12px;")
        .arg(GlassTheme::instance().textSecond().name(QColor::HexRgb)));
    toastLbl_->hide();
    root->addWidget(toastLbl_);
    toastTimer_ = new QTimer(this);
    toastTimer_->setSingleShot(true);
    toastTimer_->setInterval(2000);
    connect(toastTimer_, &QTimer::timeout, toastLbl_, &QLabel::hide);

    // ---------------- 连接确认弹窗接线（向日葵模式） ----------------
    auto* confirm = new ConnectConfirmDialog(this);
    connect(devList, &DeviceListPage::connectRequested, this,
            [this, confirm](const QString& alias, const QString& code, bool trusted) {
                if (trusted || (trust_ != nullptr
                                && trust_->is_trusted(code.toStdString()))) {
                    showToast(QStringLiteral("正在直连 %1（已信任）…").arg(alias));
                    return;
                }
                confirm->setPeer(alias, QStringLiteral("192.168.1.45"), code);
                if (confirm->exec() == QDialog::Accepted) {
                    if (confirm->rememberChecked() && trust_ != nullptr) {
                        trust_->trust(code.toStdString(), alias.toStdString());
                    }
                    showToast(confirm->rememberChecked()
                            ? QStringLiteral("已同意，%1 已加入信任缓存").arg(alias)
                            : QStringLiteral("已同意，会话建立中…"));
                }
                else {
                    showToast(QStringLiteral("已拒绝连接"));
                }
            });
    connect(connCode, &ConnectCodePage::connectRequested, this,
            [this, confirm](const QString& code) {
                // 三段化显示：482130556 → 482 130 556
                QString pretty;
                for (int i = 0; i < code.size(); i += 3) {
                    pretty += (i > 0 ? QStringLiteral(" ") : QString())
                            + code.mid(i, 3);
                }
                confirm->setPeer(QStringLiteral("设备 %1").arg(pretty),
                                 QStringLiteral("—"), pretty);
                if (trust_ != nullptr && trust_->is_trusted(code.toStdString())) {
                    showToast(QStringLiteral("正在直连（已信任）…"));
                    return;
                }
                if (confirm->exec() == QDialog::Accepted) {
                    if (confirm->rememberChecked() && trust_ != nullptr) {
                        trust_->trust(code.toStdString(),
                                      QStringLiteral("设备 %1").arg(code).toStdString());
                    }
                    showToast(confirm->rememberChecked()
                            ? QStringLiteral("已同意，设备已加入信任缓存")
                            : QStringLiteral("已同意，会话建立中…"));
                }
                else {
                    showToast(QStringLiteral("已拒绝连接"));
                }
            });

    auto* host = new QWidget();
    host->setLayout(root);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(host);
}

QWidget* MainWindow::makeTitleBar() {
    auto* bar = new GlassPanel();
    bar->setFixedHeight(GlassTheme::kMenuBarHeight);
    bar->setStyleSheet(bar->styleSheet()
                       + QStringLiteral("QFrame#glassPanel { border-radius: 0px; "
                                        "border-top-left-radius: 20px; "
                                        "border-top-right-radius: 20px; }"));

    auto* lay = new QHBoxLayout(bar);
    lay->setContentsMargins(12, 0, 8, 0);
    lay->setSpacing(10);

    // 左：App 图标 + 标题
    auto* logo = new QLabel(QStringLiteral("S"));
    logo->setFixedSize(18, 18);
    logo->setAlignment(Qt::AlignCenter);
    logo->setStyleSheet(QStringLiteral(
        "background: %1; color: white; border-radius: 5px; font-weight: bold; font-size: 11px;")
        .arg(GlassTheme::instance().primary().name(QColor::HexRgb)));
    lay->addWidget(logo);
    auto* title = new QLabel(QStringLiteral("SC 邻桌"));
    title->setStyleSheet(QStringLiteral(
        "font-size: 16px; font-weight: 600; color: %1;")
        .arg(GlassTheme::instance().textMain().name(QColor::HexRgb)));
    lay->addWidget(title);
    lay->addStretch();

    // 中：横向菜单（macOS BS）——QMenuBar 在 QSS 下项宽计算异常（折叠成 »），
    // 改 QToolButton + QMenu 自绘；样式走全局 QSS#menuBtn（主题自适应 hover）
    auto makeMenuBtn = [bar](const QString& title) {
        auto* btn = new QToolButton(bar);
        btn->setText(title);
        btn->setObjectName(QStringLiteral("menuBtn"));
        btn->setPopupMode(QToolButton::InstantPopup);
        btn->setCursor(Qt::PointingHandCursor);
        return btn;
    };
    auto attachMenu = [](QToolButton* btn, std::initializer_list<QAction*> acts) {
        auto* m = new QMenu(btn);
        for (auto* a : acts) {
            m->addAction(a);
        }
        btn->setMenu(m);
    };
    auto* mFile = makeMenuBtn(QStringLiteral("文件"));
    attachMenu(mFile, {
        new QAction(QStringLiteral("发送文件…"), mFile),
        new QAction(QStringLiteral("退出"), mFile),
    });
    connect(mFile->menu(), &QMenu::triggered, this, [this](QAction* a) {
        if (a->text() == QStringLiteral("退出")) {
            close();
        }
        else {
            setDockIndex(3);
        }
    });
    auto* mConn = makeMenuBtn(QStringLiteral("连接"));
    attachMenu(mConn, {
        new QAction(QStringLiteral("新建连接…"), mConn),
        new QAction(QStringLiteral("断开当前会话"), mConn),
    });
    connect(mConn->menu(), &QMenu::triggered, this, [this](QAction* a) {
        if (a->text() == QStringLiteral("新建连接…")) {
            setDockIndex(1);
        }
        else {
            setDockIndex(0);
        }
    });
    auto* mView = makeMenuBtn(QStringLiteral("视图"));
    attachMenu(mView, {
        new QAction(QStringLiteral("刷新设备列表"), mView),
        new QAction(QStringLiteral("切换主题"), mView),
    });
    connect(mView->menu(), &QMenu::triggered, this, [this](QAction* a) {
        if (a->text() == QStringLiteral("切换主题")) {
            applyTheme(!GlassTheme::instance().isDark());
        }
        else {
            showToast(QStringLiteral("设备列表已刷新"));   // TODO(V1.0) discovery refresh
        }
    });
    auto* mTool = makeMenuBtn(QStringLiteral("工具"));
    attachMenu(mTool, {
        new QAction(QStringLiteral("设置…"), mTool),
        new QAction(QStringLiteral("清空信任设备缓存"), mTool),
    });
    connect(mTool->menu(), &QMenu::triggered, this, [this](QAction* a) {
        if (a->text() == QStringLiteral("设置…")) {
            setDockIndex(4);
        }
        else {
            if (trust_ != nullptr) {
                trust_->clear();
            }
            showToast(QStringLiteral("信任设备缓存已清空"));
        }
    });
    auto* mHelp = makeMenuBtn(QStringLiteral("帮助"));
    attachMenu(mHelp, {new QAction(QStringLiteral("关于 SC 邻桌"), mHelp)});
    lay->addWidget(mFile);
    lay->addWidget(mConn);
    lay->addWidget(mView);
    lay->addWidget(mTool);
    lay->addWidget(mHelp);
    lay->addStretch();

    // 右：主题切换 + 窗口按钮（样式走全局 QSS#winBtn，主题自适应；点击走 glyph）
    themeBtn_ = new QLabel(GlassTheme::instance().isDark() ? "☀" : "☾");
    themeBtn_->setObjectName(QStringLiteral("winBtn"));
    themeBtn_->setFixedSize(30, 24);
    themeBtn_->setAlignment(Qt::AlignCenter);
    themeBtn_->setToolTip(QStringLiteral("切换主题"));
    themeBtn_->setProperty("themeToggle", true);
    themeBtn_->installEventFilter(this);
    lay->addWidget(themeBtn_);

    auto makeWinBtn = [](const QString& glyph, bool isClose) {
        auto* btn = new QLabel(glyph);
        btn->setObjectName(isClose ? QStringLiteral("winBtnClose")
                                   : QStringLiteral("winBtn"));
        btn->setFixedSize(30, 24);
        btn->setAlignment(Qt::AlignCenter);
        return btn;
    };
    auto* btnMin = makeWinBtn(QStringLiteral("─"), false);
    auto* btnMax = makeWinBtn(QStringLiteral("□"), false);
    auto* btnClose = makeWinBtn(QStringLiteral("✕"), true);
    lay->addWidget(btnMin);
    lay->addWidget(btnMax);
    lay->addWidget(btnClose);

    // 窗口按钮动作 + 拖拽：标题栏空白区按下即拖拽（FramelessWindow 统一处理）
    for (auto* b : {btnMin, btnMax, btnClose}) {
        b->installEventFilter(this);
    }
    bar->installEventFilter(this);
    bar->setProperty("isTitleBar", true);

    return bar;
}

QWidget* MainWindow::makeDock() {
    auto* dock = new GlassPanel();
    dock->setFixedWidth(GlassTheme::kDockWidth);
    dock->setStyleSheet(dock->styleSheet()
                        + QStringLiteral("QFrame#glassPanel { border-radius: 0px; "
                                         "border-bottom-left-radius: 20px; }"));

    auto* lay = new QVBoxLayout(dock);
    lay->setContentsMargins(0, 52, 0, 0);
    // 条目 46px 圆 + 间距 20：视觉节奏 66px 对齐 QML（56px 条目 + 10 间距，
    // 46px 圆内缩 5px）；hover 高亮圆与 QML 同为 46px
    lay->setSpacing(20);
    lay->setAlignment(Qt::AlignHCenter);

    // 条目与内容栈顺序严格对齐：0 设备列表 / 1 新建连接 / 2 远程画面 /
    // 3 文件传输 / 4 设置（关于走菜单，不入 Dock）
    const QStringList glyphs = {QStringLiteral("◱"), QStringLiteral("⧉"),
                                QStringLiteral("▣"), QStringLiteral("⇅"),
                                QStringLiteral("⚙")};
    const QStringList tips = {QStringLiteral("设备列表"), QStringLiteral("新建连接"),
                              QStringLiteral("远程画面"), QStringLiteral("文件传输"),
                              QStringLiteral("设置")};

    for (int i = 0; i < glyphs.size(); ++i) {
        auto* btn = new QLabel(glyphs.at(i));
        btn->setFixedSize(46, 46);
        btn->setAlignment(Qt::AlignCenter);
        btn->setStyleSheet(QStringLiteral("font-size: 24px; color: %1; border-radius: 23px;")
                               .arg(GlassTheme::instance().textSecond().name(QColor::HexRgb)));
        btn->setProperty("pageIndex", i);
        btn->setToolTip(tips.at(i));
        btn->setAttribute(Qt::WA_Hover, true);
        btn->installEventFilter(this);
        lay->addWidget(btn);
        dockBtns_.append(btn);

        // 选中标记：左缘主色竖线（对齐 QML DockBar §8）
        auto* bar = new QLabel(btn);
        bar->setGeometry(2, 13, 3, 20);
        bar->setStyleSheet(QStringLiteral(
            "background: %1; border-radius: 2px;")
            .arg(GlassTheme::instance().primary().name(QColor::HexRgb)));
        bar->setVisible(i == 0);
        dockBars_.append(bar);
    }
    // 初始样式仅设底色（选中态待 stack_ 建立后由 updateDockStyle 统一刷，
    // 此处 stack_ 尚未创建，读其 currentIndex 会空指针崩溃）
    for (int i = 0; i < dockBtns_.size(); ++i) {
        auto* btn = dockBtns_.at(i);
        btn->setProperty("baseQss", QStringLiteral("font-size: 24px; color: %1; border-radius: 23px;")
                                        .arg(GlassTheme::instance().textSecond().name(QColor::HexRgb)));
        btn->setProperty("hoverQss", QStringLiteral("font-size: 24px; color: %1; border-radius: 23px; background: %2;")
                                         .arg(GlassTheme::instance().textSecond().name(QColor::HexRgb),
                                              GlassTheme::instance().hoverBg().name(QColor::HexArgb)));
        btn->setStyleSheet(btn->property("baseQss").toString());
    }
    lay->addStretch();
    return dock;
}

void MainWindow::setDockIndex(int idx) {
    stack_->setCurrentIndex(idx);
    updateDockStyle();
}

void MainWindow::updateDockStyle() {
    if (stack_ == nullptr) {
        return;  // 栈未建立（构造期调用），仅底色兜底
    }
    // 选中项图标染主色 + hover 底色，未选中次色（样式随选中态刷新；
    // hover 底色取主题 hoverBg，深浅主题自适应——对齐 QML Theme.hoverBg）
    const auto primary = GlassTheme::instance().primary().name(QColor::HexRgb);
    const auto second = GlassTheme::instance().textSecond().name(QColor::HexRgb);
    const auto hover = GlassTheme::instance().hoverBg().name(QColor::HexArgb);
    for (int i = 0; i < dockBtns_.size(); ++i) {
        auto* btn = dockBtns_.at(i);
        const bool sel = (i == stack_->currentIndex());
        const QString color = sel ? primary : second;
        btn->setProperty("baseQss", QStringLiteral("font-size: 24px; color: %1; border-radius: 23px;").arg(color));
        btn->setProperty("hoverQss", QStringLiteral("font-size: 24px; color: %1; border-radius: 23px; background: %2;").arg(color, hover));
        btn->setStyleSheet(btn->property("baseQss").toString());
        dockBars_.at(i)->setVisible(sel);
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* e) {
    auto* w = qobject_cast<QWidget*>(watched);
    if (w == nullptr) {
        return FramelessWindow::eventFilter(watched, e);
    }
    // hover 高亮（120ms 由 QSS 过渡近似即时，无碍对照）
    if (e->type() == QEvent::HoverEnter && w->property("hoverQss").isValid()) {
        w->setStyleSheet(w->property("hoverQss").toString());
    }
    else if (e->type() == QEvent::HoverLeave && w->property("baseQss").isValid()) {
        w->setStyleSheet(w->property("baseQss").toString());
    }
    // 窗口按钮点击
    else if (e->type() == QEvent::MouseButtonRelease) {
        auto* lbl = qobject_cast<QLabel*>(w);
        const QString g = lbl ? lbl->text() : QString();
        if (g == QStringLiteral("─")) {
            showMinimized();
        }
        else if (g == QStringLiteral("□")) {
            isMaximized() ? showNormal() : showMaximized();
        }
        else if (g == QStringLiteral("✕")) {
            close();
        }
        else if (w->property("pageIndex").isValid()) {
            setDockIndex(w->property("pageIndex").toInt());
        }
        else if (w->property("themeToggle").isValid()) {
            applyTheme(!GlassTheme::instance().isDark());
        }
    }
    // 标题栏拖拽
    else if (e->type() == QEvent::MouseButtonPress
             && w->property("isTitleBar").isValid()) {
        auto* me = static_cast<QMouseEvent*>(e);
        beginWindowDrag(me->globalPosition().toPoint());
    }
    return FramelessWindow::eventFilter(watched, e);
}

void MainWindow::applyTheme(bool dark) {
    GlassTheme::instance().setDark(dark);
    // 全局 QSS 一次刷新：menuBtn/winBtn/deviceCard/列表 hover 全部随主题
    qApp->setStyleSheet(GlassTheme::instance().buildQss());
    // 标题栏按钮 glyph：深色显示太阳（点击回浅色），浅色显示月亮
    themeBtn_->setText(dark ? QStringLiteral("☀") : QStringLiteral("☾"));
    updateDockStyle();   // Dock 选中竖线/图标染色/hover 随主题
    update();
}

void MainWindow::showToast(const QString& text) {
    toastLbl_->setText(text);
    toastLbl_->show();
    toastTimer_->start();
}

}  // namespace sc::localdesk::ui
