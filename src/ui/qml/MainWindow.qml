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
import QtQuick.Window
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
    property string pendingIp: ""     // 入站请求方 IP（transport 对端地址直出）
    // PRD P7 控制端状态条：connecting/wait_confirm/rejected 三态
    property string connectStage: ""
    property string connectAlias: ""
    // 文件接收确认（Doc11 §4.3）
    property string pendingFileId: ""

    // 启动回放：主题持久化（[appearance]，Doc18 §3.3）+ 端口占用提示（Doc18 风险清单 P1）
    Component.onCompleted: {
        if (ScLocaldesk.settingsValue("appearance.theme") === "dark" && !Theme.isDark) {
            Theme.toggleDark();
        }
        if (ScLocaldesk.settingsValue("appearance.reduce_transparency") === "true") {
            Theme.reducedTransparency = true;
        }
        if (!ScLocaldesk.listening) {
            // 端口占用（Doc18 风险清单 P1）：10s 长驻告警 + 具体端口指引
            toast.show("会话端口 " + ScLocaldesk.settingsValue("ports.ws_port")
                       + " 监听失败，请在 设置→网络 更换端口后重启", 10000);
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
                        if (ScLocaldesk.sessionBusy) {
                            toast.show("当前有会话进行中，请先断开");  // PRD P7 防重复
                            return;
                        }
                        if (trusted || ScLocaldesk.isTrusted(code)) {
                            toast.show("正在直连 " + aliasName + "（已信任）…");
                            ScLocaldesk.startControl(aliasName, false, "");
                        }
                        else {
                            pendingAlias = aliasName;
                            pendingCode = code;
                            confirmPopup.outgoing = true;   // 控制端发起：同意即 startControl
                            confirmPopup.open();
                        }
                    }
                }

                Page_Connect {
                    onConnectRequested: code => {
                        if (ScLocaldesk.sessionBusy) {
                            toast.show("当前有会话进行中，请先断开");  // PRD P7 防重复
                            return;
                        }
                        const aliasName = ScLocaldesk.deviceAlias(code);
                        if (aliasName === "") {
                            toast.show("设备不在线：" + code);
                            return;
                        }
                        pendingAlias = aliasName;
                        pendingCode = code;
                        if (ScLocaldesk.isTrusted(code)) {
                            toast.show("正在直连（已信任）…");
                            ScLocaldesk.startControl(aliasName, false, "");
                        }
                        else {
                            confirmPopup.outgoing = true;
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
                    onRequestToast: text => toast.show(text)
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

        // 控制端连接状态条（PRD P7）：菜单栏下方悬浮，细分文案+取消/重连
        Rectangle {
            id: connectBar
            visible: connectStage !== ""
            anchors.top: parent.top
            anchors.topMargin: Theme.menuBarHeight + 6
            anchors.horizontalCenter: parent.horizontalCenter
            width: row.implicitWidth + 32
            height: 40
            radius: Theme.cardRadius
            color: Qt.rgba(0.13, 0.13, 0.15, 0.9)
            border.color: Qt.rgba(1, 1, 1, 0.14)
            z: 50

            Row {
                id: row
                anchors.centerIn: parent
                spacing: 14

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: connectStage === "connecting"
                              ? "正在连接 " + connectAlias + " …"
                              : connectStage === "wait_confirm"
                                  ? "等待 " + connectAlias + " 确认…"
                                  : connectAlias + " 拒绝了本次协助"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    color: connectStage === "rejected" ? Theme.accentRed : "#FFFFFF"
                }

                // 取消（拨号/等确认期）/ 重连（被拒后）
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: connectStage === "rejected" ? "重新连接" : "取消"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    color: Theme.primary

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (connectStage === "rejected") {
                                if (ScLocaldesk.lastPeerAlias() !== "") {
                                    connectStage = "";
                                    ScLocaldesk.startControl(
                                        ScLocaldesk.lastPeerAlias(), false, "");
                                }
                            }
                            else {
                                ScLocaldesk.cancelConnect();  // PRD P7 取消
                                connectStage = "";
                                toast.show("已取消连接");
                            }
                        }
                    }
                }
            }
        }
        Timer {
            id: connectHideTimer
            interval: 4000
            onTriggered: connectStage = ""
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
        peerIp: pendingIp
        onClosed: ScLocaldesk.setSelfWindowTopMost(false)  // 同意/拒绝/超时关闭统一释放置顶

        onAccepted: remember => {
            if (confirmPopup.outgoing) {
                // 控制端发起：确认后真正拨号（Doc18 §4.3；密码模式走无人值守弹窗）
                ScLocaldesk.startControl(pendingAlias, false, "");
                toast.show("正在连接 " + pendingAlias + " …");
            }
            else {
                // 被控端入站申请
                if (remember) {
                    ScLocaldesk.trust(pendingCode, pendingAlias);   // 确认之后才有：落盘 trust.json
                    toast.show("已同意，" + pendingAlias + " 已加入信任缓存");
                }
                else {
                    toast.show("已同意，会话建立中…");
                }
                ScLocaldesk.approve();
            }
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

    // 文件接收确认弹窗（confirm_before_transfer，Doc11 §4.3）
    Popup_FileConfirm {
        id: fileConfirmPopup
        glassSource: shell
        onAccepted: {
            ScLocaldesk.acceptFile(pendingFileId);
            toast.show("已同意接收，保存至 设置→文件 指定目录");
        }
        onRejected: {
            ScLocaldesk.rejectFile(pendingFileId);
            toast.show("已拒绝接收");
        }
    }

    // ---------------- Toast ----------------
    Toast {
        id: toast
        x: 400   // 保守固定位（DPI 下动态锚定不可靠，见 Toast.qml 注 2）
        y: 520
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
        function onConnectRequest(alias, ip) {
            pendingAlias = alias;
            pendingCode = ScLocaldesk.deviceCode(alias);  // 对方连接码核对行（在线表反查）
            pendingIp = ip;
            confirmPopup.outgoing = false;   // 入站申请：同意即 approve
            // PRD 确认三件套：系统级置顶（弹窗依附窗口层，窗口不置顶必被盖）
            ScLocaldesk.setSelfWindowTopMost(true);
            confirmPopup.open();
        }
        function onConnectRequestTimedOut() {
            // PRD 确认三件套：30s 未响应服务端已自动拒，收尾弹窗
            confirmPopup.close();
            toast.show("30 秒未响应，已自动拒绝");
        }
        function onConnectDenied(alias, reason) {
            toast.show((alias === "" ? "" : alias + "：") + reason);
        }
        function onControlStartFailed(reason) {
            // PRD P5 寻址失败细分：自连 / 会话占用 / 设备不在线
            toast.show(reason);
            connectStage = "";
        }
        function onConnectStateChanged(stage, alias) {
            // PRD P7 状态流转：connecting/wait_confirm/rejected → 顶部状态条
            connectAlias = alias;
            if (stage === "connecting" || stage === "wait_confirm") {
                connectStage = stage;
                connectHideTimer.stop();
            }
            else {
                // rejected：状态条转失败文案（带重连按钮），4s 后自动收
                connectStage = "rejected";
                if (stage === "rejected") {
                    toast.show(alias + " 拒绝了本次协助");
                }
                connectHideTimer.restart();
            }
        }
        function onSessionStarted(alias) {
            connectStage = "";               // 会话建立：连接条收起（PRD P7）
            dockBar.currentIndex = 2;        // 切远程画面页（管线已在跑）
            toast.show("会话已建立：" + alias);
        }
        function onAliasConflict(alias, conflictIp) {
            // PRD P5 别名冲突：引导改名（设置 → 本机连接名）
            toast.show("检测到别名冲突：" + alias + "（" + conflictIp
                       + "），请在设置中修改本机连接名");
        }
        function onSettingsRejected(key, why) {
            toast.show("保存被拒绝：" + why);
        }
        function onSessionClosed(reason) {
            if (dockBar.currentIndex === 2) {
                dockBar.currentIndex = 0;    // 断开回设备列表
            }
            connectStage = "";               // 对端断开/网络中断：连接条一并收起（PRD P8）
            toast.show("会话已结束：" + reason);
        }
        function onFileMetaReceived(fileId, name, size, alias) {
            pendingFileId = fileId;
            fileConfirmPopup.peerAlias = alias === "" ? "对方" : alias;
            fileConfirmPopup.fileName = name;
            fileConfirmPopup.fileSize = size;
            fileConfirmPopup.open();
        }
    }
}
