// ============================================================
// scToLocalDesk | src/ui/qml/popups/Popup_ConnectConfirm.qml
// 被控端连接同意弹窗（向日葵模式）：18px 毛玻璃 + 弱阴影
// 显示连接名/连接码/IP；【同意】可勾选"记住此设备"进信任缓存
// （确认后才有记忆缓存——产品需求 2025-10）
// ============================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Popup {
    id: root

    property string peerAlias: ""
    property string peerCode: ""
    property string peerIp: ""
    property Item glassSource

    /// remember=勾选了"记住此设备"（V1.0 TODO 写入 TrustStore）
    signal accepted(bool remember)
    signal rejected()

    anchors.centerIn: Overlay.overlay
    width: 400
    height: 268
    modal: true
    closePolicy: Popup.NoAutoClose

    background: GlobalGlass {
        glassSource: root.glassSource
        radiusPx: Theme.cardRadius
        shadowed: true
    }

    contentItem: ColumnLayout {
        spacing: 10

        Text {
            text: "连接请求"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Medium
            color: Theme.textMain
        }
        Text {
            text: root.peerAlias + " 请求远程协助本机"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            color: Theme.textSecond
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }

        // 连接码行（双向核对依据）
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            radius: Theme.btnRadius
            color: Theme.winBg
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8
                Text {
                    text: "对方连接码"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontHint
                    color: Theme.textHint
                }
                Item {
                    Layout.fillWidth: true
                }
                Text {
                    text: root.peerCode
                    font.family: Theme.fontFamily
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    font.letterSpacing: 2
                    color: Theme.primary
                }
            }
        }
        Text {
            text: "IP：" + root.peerIp
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontHint
            color: Theme.textHint
        }

        // 信任记忆复选框（确认后才有记忆缓存）
        CheckBox {
            id: rememberChk
            text: "记住此设备，下次免确认直连"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            checked: true

            indicator: Rectangle {
                implicitWidth: 18
                implicitHeight: 18
                radius: 4
                color: rememberChk.checked ? Theme.primary : Theme.winBg
                border.color: rememberChk.checked ? Theme.primary : Theme.divider
                border.width: 1
                Behavior on color {
                    ColorAnimation {
                        duration: Theme.durHover
                    }
                }
                Text {
                    anchors.centerIn: parent
                    text: "✓"
                    color: "#FFFFFF"
                    font.pixelSize: 12
                    font.bold: true
                    visible: rememberChk.checked
                }
            }
            contentItem: Text {
                text: rememberChk.text
                font: rememberChk.font
                color: Theme.textMain
                leftPadding: rememberChk.indicator.width + 6
                verticalAlignment: Text.AlignVCenter
            }
        }

        Item {
            Layout.fillHeight: true
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 10

            Button {
                text: "拒绝"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                onClicked: {
                    root.close();
                    root.rejected();
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
                    scale: parent.pressed ? 0.97 : 1.0
                    Behavior on scale {
                        NumberAnimation {
                            duration: 80
                        }
                    }
                }
            }
            Button {
                text: "同意"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                onClicked: {
                    root.close();
                    root.accepted(rememberChk.checked);
                }
                contentItem: Text {
                    text: parent.text
                    font: parent.font
                    color: "#FFFFFF"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.btnRadius
                    color: parent.hovered ? Theme.primaryHover : Theme.primary
                    scale: parent.pressed ? 0.97 : 1.0
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
        NumberAnimation {
            property: "scale"
            from: 1
            to: 0.97
            duration: Theme.durPopup
            easing.type: Theme.easingType
        }
    }
}
