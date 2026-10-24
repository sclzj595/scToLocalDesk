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

    /// 会话中判定：有帧（frameSize 非 0）且会话未关闭
    readonly property bool inSession: sessionActive && VideoPipeline.frameSize.width > 0

    Connections {
        target: ScLocaldesk
        function onSessionStarted() {
            root.sessionActive = true;
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

        // 无会话空态：无帧时的占位提示（禁止无提示黑屏）
        Text {
            anchors.centerIn: parent
            visible: !root.inSession
            text: "暂未进行远程控制\n从设备列表或连接码页发起会话后，画面将显示在此处"
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
        // 无会话隐藏（断开按钮在无会话时不可用）
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 20
            visible: root.inSession
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

                ToolButton {
                    id: fullBtn
                    text: "⛶ 全屏"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
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
                    text: "断开连接"
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
