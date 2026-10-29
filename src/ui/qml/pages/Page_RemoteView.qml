// ============================================================
// scToLocalDesk | src/ui/qml/pages/Page_RemoteView.qml
// 远程桌面画面控制页（Doc13 §四.3）：画面铺满、纯色底、禁玻璃
// 底部悬浮工具条（半透非模糊，不触玻璃红线）
// ============================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."
import ScLocaldesk 1.0

Item {
    id: root

    signal disconnected()

    /// 会话生命周期（MainWindow 桥接 ScLocaldesk.sessionStarted/Closed）：
    /// frameSize 在会话关闭后保持末帧，须叠加本状态才能正确回空态
    property bool sessionActive: false
    /// 本端角色 + 对端别名（onSessionStarted 快照）：空态文案分流——
    /// 控制端无帧（对方未开画面/无采集）≠ 被控端（提示被控中）
    property bool imController: true
    property string peerAlias: ""

    /// 会话中判定：有帧（frameSize 非 0）且会话未关闭
    readonly property bool inSession: sessionActive && VideoPipeline.frameSize.width > 0

    Connections {
        target: ScLocaldesk
        function onSessionStarted(alias) {
            root.sessionActive = true;
            root.peerAlias = alias;
            root.imController = ScLocaldesk.isController();
        }
        function onSessionClosed() {
            root.sessionActive = false;
        }
    }

    Rectangle {
        id: screenArea
        anchors.fill: parent
        color: "#000000"

        // 远程画面（Doc18 §4.2）：VideoPipeline.frameReady → 等比铺满
        VideoSurface {
            id: surface
            anchors.fill: parent
            pipeline: VideoPipeline
        }

        // 空态三分支（禁止无提示黑屏）：无会话占位 / 控制端等画面 /
        // 被控端提示被控中（会话活着但无帧 ≠ 无会话）
        Text {
            anchors.centerIn: parent
            visible: !root.inSession
            text: !root.sessionActive
                  ? qsTr("暂未进行远程控制\n从设备列表或连接码页发起会话后，画面将显示在此处")
                  : root.imController
                      ? qsTr("已连接 %1，等待对方画面…\n（对方未开启画面共享或对方暂无采集能力）").arg(root.peerAlias)
                      : qsTr("正在被 %1 远程控制").arg(root.peerAlias)
            horizontalAlignment: Text.AlignHCenter
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            color: "#666666"
        }

        // 键鼠捕获（Doc18 §4.4）：盖住画面，归一化后经桥发送；
        // 声明于工具条之前 → 工具条仍优先收事件
        RemoteInputItem {
            id: inputCatcher
            anchors.fill: parent
            visible: root.inSession
            videoSize: VideoPipeline.frameSize
            focus: true
            onVisibleChanged: if (visible) forceActiveFocus()
            onInputMouse: (action, x, y, wheel) =>
                          ScLocaldesk.sendMouse(action, x, y, wheel)
            onInputKey: (vk, scan, down) => ScLocaldesk.sendKey(vk, scan, down)
        }

        // 底部悬浮工具条：半透底 + 描边（内容区禁模糊，Doc13 红线2）；
        // 会话期恒显（无帧会话也必须能断开，Doc16 §2.6 一键断开红线）
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 20
            z: 10  // 压过键鼠捕获层：工具条点击必达（历史“点不动”防御）
            visible: root.sessionActive
            width: toolRow.implicitWidth + 32
            height: 44
            radius: Theme.cardRadius
            color: Theme.isDark ? Qt.rgba(0.10, 0.11, 0.13, 0.72)
                                : Qt.rgba(1, 1, 1, 0.72)
            border.color: Theme.glassBorder
            border.width: 1

            Row {
                id: toolRow
                anchors.centerIn: parent
                spacing: 8

                // NoFocus 红线：按钮不吃键盘焦点——否则点击后 Space/Enter
                // 变成激活按钮、Tab 在按钮间导航，远端收不到这些键
                //（“空格回车 tab 有时卡顿”真因）
                ToolButton {
                    id: fullBtn
                    focusPolicy: Qt.NoFocus
                    text: qsTr("⛶ 全屏")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    onClicked: {
                        const w = root.Window.window;
                        if (w) {
                            w.visibility === Window.FullScreen
                                ? w.showNormal() : w.showFullScreen();
                        }
                    }
                    contentItem: Text {
                        text: fullBtn.text
                        font: fullBtn.font
                        color: Theme.textMain
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Theme.btnRadius
                        color: fullBtn.hovered ? Theme.hoverBg : "transparent"
                        Behavior on color {
                            ColorAnimation {
                                duration: Theme.durHover
                            }
                        }
                    }
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 1
                    height: 20
                    color: Theme.divider
                }

                Button {
                    id: discBtn
                    focusPolicy: Qt.NoFocus
                    text: qsTr("断开连接")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    onClicked: root.disconnected()
                    contentItem: Text {
                        text: discBtn.text
                        font: discBtn.font
                        color: "#FFFFFF"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Theme.btnRadius
                        color: discBtn.hovered ? Qt.lighter(Theme.accentRed, 1.10)
                                               : Theme.accentRed
                        scale: discBtn.pressed ? 0.97 : 1.0
                        Behavior on color {
                            ColorAnimation {
                                duration: Theme.durHover
                            }
                        }
                        Behavior on scale {
                            NumberAnimation {
                                duration: 80
                            }
                        }
                    }
                }
            }
        }
    }
}
