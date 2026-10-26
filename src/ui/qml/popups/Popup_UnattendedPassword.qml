// ============================================================
// scToLocalDesk | src/ui/qml/popups/Popup_UnattendedPassword.qml
// 无人值守密码验证弹窗（Doc13 §四.7 / Doc16）：18px 毛玻璃
// 密码输入支持显隐切换；验证走 AUTH_CHALLENGE/PROOF 协议
// ============================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Popup {
    id: root

    property Item glassSource
    property string peerAlias: ""

    signal verified(string password)
    signal cancelled()

    anchors.centerIn: Overlay.overlay
    width: 360
    height: 210
    modal: true
    closePolicy: Popup.CloseOnEscape

    background: GlobalGlass {
        glassSource: root.glassSource
        radiusPx: Theme.cardRadius
        shadowed: true
    }

    contentItem: ColumnLayout {
        spacing: 12

        Text {
            text: qsTr("无人值守验证")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Medium
            color: Theme.textMain
        }
        Text {
            text: qsTr("请输入 ") + root.peerAlias + " 的无人值守密码"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            color: Theme.textSecond
        }

        RowLayout {
            spacing: 8
            TextField {
                id: pwd
                Layout.fillWidth: true
                echoMode: revealBtn.checked ? TextInput.Normal : TextInput.Password
                selectByMouse: true
                placeholderText: qsTr("无人值守密码")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                color: Theme.textMain
                placeholderTextColor: Theme.textHint
                leftPadding: 12
                rightPadding: 12
                onAccepted: root.verified(text)
                background: Rectangle {
                    radius: Theme.btnRadius
                    color: Theme.winBg
                    border.color: pwd.activeFocus ? Theme.primary : Theme.divider
                    border.width: 1
                    Behavior on border.color {
                        ColorAnimation {
                            duration: Theme.durHover
                        }
                    }
                }
            }
            // 显隐切换
            ToolButton {
                id: revealBtn
                checkable: true
                text: checked ? "👁" : "◔"
                font.pixelSize: 14
                contentItem: Text {
                    text: revealBtn.text
                    font: revealBtn.font
                    color: Theme.textSecond
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.btnRadius
                    color: revealBtn.hovered ? Theme.hoverBg : "transparent"
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 10

            Button {
                text: qsTr("取消")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                onClicked: {
                    root.close();
                    root.cancelled();
                }
                contentItem: Text {
                    text: parent.text
                    font: parent.font
                    color: Theme.textSecond
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.btnRadius
                    color: Theme.cardBg
                    border.color: Theme.divider
                    border.width: 1
                }
            }
            Button {
                text: qsTr("验证")
                enabled: pwd.text.length > 0
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                onClicked: root.verified(pwd.text)
                contentItem: Text {
                    text: parent.text
                    font: parent.font
                    color: "#FFFFFF"
                    opacity: parent.enabled ? 1.0 : 0.45
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.btnRadius
                    color: parent.enabled
                           ? (parent.hovered ? Theme.primaryHover : Theme.primary)
                           : Theme.primaryDisabled
                    Behavior on color {
                        ColorAnimation {
                            duration: Theme.durHover
                        }
                    }
                }
            }
        }
    }

    onOpened: pwd.forceActiveFocus()

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.durPopup
            easing.type: Theme.easingType
        }
        NumberAnimation {
            property: "scale"
            from: 0.97
            to: 1
            duration: Theme.durPopup
            easing.type: Theme.easingType
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: Theme.durPopup
            easing.type: Theme.easingType
        }
    }
}
