// ============================================================
// scToLocalDesk | src/ui/qwidget/widgets_main.cpp
// QWidget 对照版入口（Doc13 §13.2）：与 QML 版同套视觉规范
// ============================================================
#ifdef SCLD_QT_APP
    #ifdef SCLD_QT_WIDGETS_UI

        #include <QApplication>
        #include <QCoreApplication>
        #include <QFileInfo>

        #include "foundation/config_parser.hpp"
        #include "platform/qt_adapter/single_instance_guard.hpp"
        #include "service/app_config.hpp"
        #include "service/trust_store.hpp"
        #include "ui/qwidget/glass_theme.h"
        #include "ui/qwidget/main_window.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // 单实例锁（exe 名作 key：与 QML 版互不排斥，对照构建可并存）
    sc::localdesk::platform::SingleInstanceGuard guard;
    if (!guard.acquire(QFileInfo(QApplication::applicationFilePath()).fileName())) {
        return 0;   // 已有实例：唤起请求已投递
    }

    // 配置加载（Doc17：exe 旁 config.ini，缺失则默认值落盘；Doc18 §3.3 设置页生命周期）
    sc::localdesk::foundation::IniConfig ini(
        QCoreApplication::applicationDirPath().toStdWString() + L"\\config.ini");
    sc::localdesk::AppConfig cfg;
    if (!ini.load()) {
        cfg.save(ini);
        (void)ini.save();
    }
    else {
        cfg.load(ini);
    }

    // 信任缓存：exe 旁 trust.json（Doc18 §3.2，确认弹窗勾选记忆落盘）
    sc::localdesk::TrustStore trust_store(
        QCoreApplication::applicationDirPath().toStdWString() + L"\\trust.json");

    // 全局 QSS 一次性挂载（主题切换时重建，Doc13 §7）
    app.setStyleSheet(sc::localdesk::ui::GlassTheme::instance().buildQss());

    sc::localdesk::ui::MainWindow win;
    win.setTrustStore(&trust_store);
    win.set_config_store(&ini, &cfg);
    win.show();

    // 单实例唤起：二次启动把首实例主窗带到前台
    guard.set_raise_callback([&win] {
        win.showNormal();
        win.raise();
        win.activateWindow();
    });

    // TODO(V1.0): 复用 main.cpp 的 DI 组装（core 服务全量接线；
    // 演示期仅 UI 对照，无会话/发现服务）
    return app.exec();
}

    #endif
#endif
