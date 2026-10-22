// ============================================================
// scToLocalDesk | src/ui/ui_main.cpp
// QML 界面组装：bridge 注册 / 页面加载（Doc13 §2）
// ============================================================
#include <QQmlApplicationEngine>
#include <QQmlContext>

namespace sc::localdesk::ui {

/// UI 模块初始化：注册 QmlBridge 单例，加载主页面
/// QML 不直接调用 core；全部经 bridge 信号转发（Doc13 §4 红线）
void init_ui(QQmlApplicationEngine& engine) {
    // TODO(V1.0):
    //   1. qmlRegisterSingletonInstance("ScLocaldesk", 1, 0, "Bridge", &bridge)
    //   2. engine.load(QUrl("qrc:/scToLocalDesk/qml/MainPage.qml"))
    //   3. ConfirmDialog 弹窗置顶（WindowAlwaysOnTop flag）
    (void)engine;
}

}  // namespace sc::localdesk::ui
