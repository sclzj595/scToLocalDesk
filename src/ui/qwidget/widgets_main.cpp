// ============================================================
// scToLocalDesk | src/ui/qwidget/widgets_main.cpp
// QWidget 对照版入口（Doc13 §13.2）：与 QML 版同套视觉规范
// ============================================================
#ifdef SCLD_QT_APP
    #ifdef SCLD_QT_WIDGETS_UI

        #include <QApplication>

        #include "platform/qt_adapter/qt_event_bridge.hpp"
        #include "ui/qwidget/glass_theme.h"
        #include "ui/qwidget/main_window.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // 全局 QSS 一次性挂载（主题切换时重建，Doc13 §7）
    app.setStyleSheet(sc::localdesk::ui::GlassTheme::instance().buildQss());

    sc::localdesk::ui::MainWindow win;
    win.show();

    // TODO(V1.0): 复用 main.cpp 的 DI 组装（core 服务全量接线；
    // 演示期仅 UI 对照，无会话/发现服务）
    return app.exec();
}

    #endif
#endif
