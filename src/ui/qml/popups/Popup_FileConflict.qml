// ============================================================
// scToLocalDesk | src/ui/qml/popups/Popup_FileConflict.qml
// 同名文件冲突三选项弹窗（Doc19 §5）：覆盖 / 重命名 / 跳过 +
// 「记住本次选择」（仅会话级记忆，不落盘 config，断开自动失效）
// AppDialog 基座承载视觉，与 Popup_FileConfirm 同风格
// ============================================================
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import ".."

AppDialog {
    id: root

    property string peerAlias: ""
    property string fileName: ""
    property real fileSize: 0
    property Item glassSource

    signal resolved(string policy, bool remember)

    width: 400
    height: 250
    title: qsTr("同名文件冲突")

    function humanSize(bytes) {
        if (bytes >= 1073741824) return (bytes / 1073741824).toFixed(2) + " GB";
        if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + " MB";
        if (bytes >= 1024) return (bytes / 1024).toFixed(1) + " KB";
        return bytes + " B";
    }

    Text {
        text: qsTr("%1 发送的文件已存在").arg(root.peerAlias)
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        color: Theme.textSecond
        Layout.fillWidth: true
        wrapMode: Text.Wrap
    }

    // 文件行（与 Popup_FileConfirm 同款）
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

    // 「记住本次选择」勾选（会话级，Doc19 §5：不落盘 config）
    CheckBox {
        id: rememberChk
        text: qsTr("记住本次选择（本次会话有效）")
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontHint
        Layout.fillWidth: true
    }

    footerChildren: [
        AppButton {
            primary: false
            text: qsTr("跳过")
            onClicked: {
                root.close();
                root.resolved("skip", rememberChk.checked);
            }
        },
        AppButton {
            primary: false
            text: qsTr("重命名保存")
            onClicked: {
                root.close();
                root.resolved("rename", rememberChk.checked);
            }
        },
        AppButton {
            text: qsTr("覆盖")
            onClicked: {
                root.close();
                root.resolved("overwrite", rememberChk.checked);
            }
        }
    ]
}
