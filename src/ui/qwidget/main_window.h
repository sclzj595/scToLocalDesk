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
class RemoteSessionService;
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
class FileTransferPage;

class MainWindow final : public FramelessWindow {
    Q_OBJECT

public:
    explicit MainWindow();

    /// 信任缓存注入（widgets_main 构造后调用，Doc18 §3.2）；
    /// 转发连接码页刷新"最近连接"真数据
    void setTrustStore(TrustStore* ts);

    /// 配置注入（Doc18 §3.3）：转发设置页，解锁控件真值回放与落盘
    void set_config_store(foundation::IniConfig* ini, AppConfig* cfg);

    /// 会话服务注入（PRD P7）：确认弹窗 Accepted 后真正发起 connect_to_peer
    void setSessionService(RemoteSessionService* s) {
        sessions_ = s;
    }

    /// 设备列表页访问（widgets_main 装配发现栈后接真数据，Doc18 §3.4）
    DeviceListPage* devicePage() const {
        return devList_;
    }

    /// 设置页访问（widgets_main 注入 AliasService，静态映射热生效）
    SettingsPage* settingsPage() const {
        return settingsPage_;
    }

    /// 远程画面页访问（widgets_main 接解码管线 frameReady，Doc18 §4.2）
    RemoteViewPage* remoteViewPage() const {
        return remoteView_;
    }

    /// 文件传输页访问（widgets_main 接 Evt_File* + 发送入口，Doc18 §5）
    FileTransferPage* fileTransferPage() const {
        return filePage_;
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    /// 关窗缩托盘（PRD §5.4 / Doc17 app.minimize_to_tray）：true 时隐藏窗口
    /// 服务常驻，托盘「退出程序」为唯一退出通道；false 保持系统默认
    void closeEvent(QCloseEvent* e) override;
    /// 最小化缩托盘：同一开关控制，隐藏而非驻留任务栏
    void changeEvent(QEvent* e) override;

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
    RemoteSessionService* sessions_{nullptr};
    AppConfig* cfg_{nullptr};  // 关窗/最小化缩托盘开关（app.minimize_to_tray）
    SettingsPage* settingsPage_{nullptr};
    DeviceListPage* devList_{nullptr};
    RemoteViewPage* remoteView_{nullptr};
    ConnectCodePage* connCode_{nullptr};
    FileTransferPage* filePage_{nullptr};
};

}  // namespace sc::localdesk::ui
