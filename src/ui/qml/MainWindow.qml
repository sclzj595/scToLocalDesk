// ============================================================
// scToLocalDesk | src/ui/qml/MainWindow.qml
// 主无边框窗口（Doc13 §四.1）：毛玻璃 MenuBar + 左侧 Dock +
// 纯色内容栈 + 连接确认弹窗 + Toast
// 分层红线：玻璃仅导航/弹窗（顶层），内容层纯色无模糊（红线2）
// 向日葵模式：未信任设备弹确认（可勾选记忆），已信任直连
// ============================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."
import "menu"
import "dock"
import "pages"
import "popups"
import ScLocaldesk 1.0

ApplicationWindow {
    id: window

    width: 1080
    height: 700
    minimumWidth: 860
    minimumHeight: 560
    visible: true
    title: "SC 邻桌"

    // 无边框窗口（红线4：菜单栏即标题栏）
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "transparent"   // 20px 圆角需透明底
    background: null       // 去掉 Controls 默认方形白底（圆角外露白折角的根源）

    // 信任缓存走 ScLocaldesk（TrustStore 落盘，Doc18 §3.2）；此处仅暂存待确认对端
    property string pendingAlias: ""
    property string pendingCode: ""

    // 启动回放：主题持久化（[appearance]，Doc18 §3.3）+ 端口占用提示（Doc18 风险清单 P1）
    Component.onCompleted: {
        if (ScLocaldesk.settingsValue("appearance.theme") === "dark" && !Theme.isDark) {
            Theme.toggleDark();
        }
        if (ScLocaldesk.settingsValue("appearance.reduce_transparency") === "true") {
            Theme.reducedTransparency = true;
        }
        if (!ScLocaldesk.listening) {
            toast.show("会话端口监听失败，请在 设置→网络 更换端口后重启");
        }
    }

    // 主窗口圆角容器（Doc13 §1.2：20px）
    Rectangle {
        id: shell
        anchors.fill: parent
        radius: Theme.winRadius
        color: Theme.winBg
        border.color: Theme.glassBorder
        border.width: 1

        Behavior on color {
            ColorAnimation {
                duration: Theme.durPage
            }
        }

        // ---------------- 中层：业务内容（纯色，禁玻璃，红线2） ----------------
        Item {
            id: contentLayer
            anchors.fill: parent
            anchors.topMargin: Theme.menuBarHeight
            anchors.leftMargin: Theme.dockWidth
            clip: true

            StackLayout {
                id: pageStack
                anchors.fill: parent
                currentIndex: dockBar.currentIndex

                Page_DeviceList {
                    onConnectRequested: (aliasName, code, trusted) => {
                        if (trusted || ScLocaldesk.isTrusted(code)) {
                            toast.show("正在直连 " + aliasName + "（已信任）…");
                        }
                        else {
                            pendingAlias = aliasName;
                            pendingCode = code;
                            confirmPopup.open();
                        }
                    }
                }

                Page_Connect {
                    onConnectRequested: code => {
                        pendingAlias = "设备 " + code;
                        pendingCode = code;
                        if (ScLocaldesk.isTrusted(code)) {
                            toast.show("正在直连（已信任）…");
                        }
                        else {
                            confirmPopup.open();
                        }
                    }
                }

                Page_RemoteView {
                    onDisconnected: {
                        ScLocaldesk.disconnectSession();
                        toast.show("会话已断开");
                    }
                }

                Page_FileTransfer {
                }

                Page_Settings {
                }
            }
        }

        // ---------------- 顶层：玻璃导航（MenuBar + Dock） ----------------
        Rectangle {
            id: menuBarBg
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.menuBarHeight
            color: "transparent"
        }

        GlobalGlass {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.menuBarHeight
            glassSource: shell
            radiusPx: 0

            MenuBarRow {
                anchors.fill: parent
                glassSource: shell
                onMenuAction: action => handleMenu(action)
                onThemeToggled: Theme.toggleDark()
            }
        }

        GlobalGlass {
            id: dockGlass
            anchors.top: menuBarBg.bottom
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            width: Theme.dockWidth
            glassSource: shell
            radiusPx: 0

            DockBar {
                id: dockBar
                anchors.fill: parent
                glassSource: shell
                onPageRequested: idx => pageStack.currentIndex = idx
            }
        }
    }

    // ---------------- 弹窗：连接确认（18px 玻璃 + 弱阴影） ----------------
    Popup_ConnectConfirm {
        id: confirmPopup
        glassSource: shell
        peerAlias: pendingAlias
        peerCode: pendingCode
        peerIp: pendingCode === "" ? "" : ScLocaldesk.deviceIp(pendingCode)

        onAccepted: remember => {
            if (remember) {
                ScLocaldesk.trust(pendingCode, pendingAlias);   // 确认之后才有：落盘 trust.json
                toast.show("已同意，" + pendingAlias + " 已加入信任缓存");
            }
            else {
                toast.show("已同意，会话建立中…");
            }
            ScLocaldesk.approve();
        }
        onRejected: {
            ScLocaldesk.reject();
            toast.show("已拒绝连接");
        }
    }

    // 无人值守密码弹窗（控制端连无人值守主机时）
    Popup_UnattendedPassword {
        id: unattendedPopup
        glassSource: shell
        peerAlias: pendingAlias
        onVerified: pwd => {
            ScLocaldesk.startControl(pendingAlias, true, pwd);
            toast.show("正在验证无人值守密码…");
        }
    }

    // ---------------- Toast ----------------
    Toast {
        id: toast
    }

    // 菜单动作分发（对接 bridge / 页面栈）
    function handleMenu(action) {
        switch (action) {
        case "file.send":
            dockBar.currentIndex = 3;
            break;
        case "app.quit":
            Qt.quit();
            break;
        case "conn.new":
            dockBar.currentIndex = 1;
            break;
        case "conn.close":
            ScLocaldesk.disconnectSession();
            toast.show("会话已断开");
            break;
        case "view.refresh":
            toast.show("设备列表已刷新");
            break;
        case "view.theme":
            Theme.toggleDark();
            break;
        case "view.reduce":
            Theme.reducedTransparency = !Theme.reducedTransparency;
            toast.show(Theme.reducedTransparency ? "已降低透明度" : "已恢复毛玻璃");
            break;
        case "tool.settings":
            dockBar.currentIndex = 4;
            break;
        case "tool.clear":
            ScLocaldesk.clearTrust();   // 清信任缓存（trust.json 同步重写）
            toast.show("信任缓存已清除");
            break;
        case "help.about":
            toast.show("SC 邻桌 v0.1.0 · 企业内网远程协助");
            break;
        }
    }

    // 被控端连接请求（bridge 信号 → 置顶弹窗，Doc13 交互红线）
    Connections {
        target: ScLocaldesk
        function onConnectRequest(alias) {
            pendingAlias = alias;
            confirmPopup.open();
        }
    }
}
