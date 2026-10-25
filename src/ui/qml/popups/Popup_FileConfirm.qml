// ============================================================
// scToLocalDesk | src/ui/qml/popups/Popup_FileConfirm.qml
// 文件接收确认弹窗（confirm_before_transfer，Doc11 §4.3）：
// 18px 毛玻璃 + 弱阴影；显示发送方/文件名/大小，【同意】接收
// ============================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Popup {
    id: root

    property string peerAlias: ""
    property string fileName: ""
    property real fileSize: 0
    property Item glassSource

    signal accepted()
    signal rejected()

    anchors.centerIn: Overlay.overlay
    width: 400
    height: 216
    modal: true
    closePolicy: Popup.NoAutoClose

    background: GlobalGlass {
        glassSource: root.glassSource
        radiusPx: Theme.cardRadius
        shadowed: true
    }

    function humanSize(bytes) {
        if (bytes >= 1073741824) return (bytes / 1073741824).toFixed(2) + " GB";
        if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + " MB";
        if (bytes >= 1024) return (bytes / 1024).toFixed(1) + " KB";
        return bytes + " B";
    }

    contentItem: ColumnLayout {
        spacing: 10

        Text {
            text: "文件接收请求"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Medium
            color: Theme.textMain
        }
        Text {
            text: root.peerAlias + " 向本机发送文件"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            color: Theme.textSecond
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }

        // 文件行
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
                    text: root.fileName
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    color: Theme.textMain
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
                Text {
                    text: root.humanSize(root.fileSize)
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontHint
                    color: Theme.textHint
                }
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
                text: "接收"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                onClicked: {
                    root.close();
                    root.accepted();
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
