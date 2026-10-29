// ============================================================
// scToLocalDesk | src/ui/qml/popups/Popup_UnattendedPassword.qml
// 无人值守密码验证弹窗（Doc13 §四.7 / Doc16）：AppDialog 基座
// 承载视觉，本文件只留业务；密码输入支持显隐切换
// 验证走 AUTH_CHALLENGE/PROOF 协议
// ============================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

AppDialog {
    id: root

    property Item glassSource
    property string peerAlias: ""

    signal verified(string password)
    signal cancelled()

    width: 360
    height: 210
    title: qsTr("无人值守验证")
    // 密码弹窗允许 Esc 取消（其余弹窗 NoAutoClose 基座默认）
    closePolicy: Popup.CloseOnEscape

    Text {
        text: qsTr("请输入 ") + root.peerAlias + " 的无人值守密码"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        color: Theme.textSecond
    }

    RowLayout {
        Layout.fillWidth: true
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

    footerChildren: [
        AppButton {
            primary: false
            text: qsTr("取消")
            onClicked: {
                root.close();
                root.cancelled();
            }
        },
        AppButton {
            text: qsTr("验证")
            enabled: pwd.text.length > 0
            onClicked: root.verified(pwd.text)
        }
    ]

    onOpened: pwd.forceActiveFocus()
}
