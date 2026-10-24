// ============================================================
// scToLocalDesk | src/ui/qwidget/main_window.h
// QWidget 版主窗口（Doc13 §四.1）：玻璃标题栏 + Dock + 内容栈
// ============================================================
#pragma once

#include "glass_panel.h"

#include <QList>

class QLabel;
class QMenuBar;
class QStackedWidget;
class QTimer;

namespace sc::localdesk {
class TrustStore;
struct AppConfig;  // 定义处为 struct，声明须一致
namespace foundation {
class IniConfig;
}
}

namespace sc::localdesk::ui {

class SettingsPage;
class DeviceListPage;
class RemoteViewPage;
class ConnectCodePage;

class MainWindow final : public FramelessWindow {
    Q_OBJECT

public:
    explicit MainWindow();

    /// 信任缓存注入（widgets_main 构造后调用，Doc18 §3.2）；
    /// 转发连接码页刷新"最近连接"真数据
    void setTrustStore(TrustStore* ts);

    /// 配置注入（Doc18 §3.3）：转发设置页，解锁控件真值回放与落盘
    void set_config_store(foundation::IniConfig* ini, AppConfig* cfg);

    /// 设备列表页访问（widgets_main 装配发现栈后接真数据，Doc18 §3.4）
    DeviceListPage* devicePage() const {
        return devList_;
    }

    /// 远程画面页访问（widgets_main 接解码管线 frameReady，Doc18 §4.2）
    RemoteViewPage* remoteViewPage() const {
        return remoteView_;
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* makeTitleBar();
    QWidget* makeDock();
    void showToast(const QString& text);
    /// Dock 选中态（对齐 QML：左缘主色竖线 + 图标变主色）
    void setDockIndex(int idx);
    /// 按当前栈页刷新 Dock 样式（选中竖线 + 图标染色）
    void updateDockStyle();
    /// 主题应用统一入口（标题栏按钮 / 菜单 / 设置页三处共用，保证联动一致）
    void applyTheme(bool dark);

    QLabel* themeBtn_{nullptr};
    QStackedWidget* stack_{nullptr};
    QList<QLabel*> dockBtns_;
    QList<QLabel*> dockBars_;
    QLabel* toastLbl_{nullptr};
    QTimer* toastTimer_{nullptr};
    TrustStore* trust_{nullptr};
    SettingsPage* settingsPage_{nullptr};
    DeviceListPage* devList_{nullptr};
    RemoteViewPage* remoteView_{nullptr};
    ConnectCodePage* connCode_{nullptr};
};

}  // namespace sc::localdesk::ui
