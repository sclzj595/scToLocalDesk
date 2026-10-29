// ============================================================
// scToLocalDesk | src/ui/qml/popups/Popup_FileConfirm.qml
// 文件接收确认弹窗（confirm_before_transfer，Doc11 §4.3）：
// AppDialog 基座承载视觉，本文件只留业务；显示发送方/文件名/大小
// ============================================================
import QtQuick
import QtQuick.Layouts
import ".."

AppDialog {
    id: root

    property string peerAlias: ""
    property string fileName: ""
    property real fileSize: 0
    property Item glassSource

    signal accepted()
    signal rejected()

    width: 400
    height: 216
    title: qsTr("文件接收请求")

    function humanSize(bytes) {
        if (bytes >= 1073741824) return (bytes / 1073741824).toFixed(2) + " GB";
        if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + " MB";
        if (bytes >= 1024) return (bytes / 1024).toFixed(1) + " KB";
        return bytes + " B";
    }

    Text {
        text: qsTr("%1 向本机发送文件").arg(root.peerAlias)
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

    footerChildren: [
        AppButton {
            primary: false
            text: qsTr("拒绝")
            onClicked: {
                root.close();
                root.rejected();
            }
        },
        AppButton {
            text: qsTr("接收")
            onClicked: {
                root.close();
                root.accepted();
            }
        }
    ]
}
