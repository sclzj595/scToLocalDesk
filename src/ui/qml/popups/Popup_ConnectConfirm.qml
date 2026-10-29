// ============================================================
// scToLocalDesk | src/ui/qml/popups/Popup_ConnectConfirm.qml
// 被控端连接同意弹窗（向日葵模式）：AppDialog 基座承载视觉
// （毛玻璃/标题/按钮行/出入场动画统一定义），本文件只留业务
// 显示连接名/连接码/IP；【同意】可勾选"记住此设备"进信任缓存
// （确认后才有记忆缓存——产品需求 2025-10）
// ============================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

AppDialog {
    id: root

    property string peerAlias: ""
    property string peerCode: ""
    property string peerIp: ""
    property bool outgoing: false  ///< true=控制端发起确认（同意即拨号），false=被控端入站申请
    property Item glassSource

    /// remember=勾选了"记住此设备"（V1.0 TODO 写入 TrustStore）
    signal accepted(bool remember)
    signal rejected()

    width: 400
    height: 268
    title: qsTr("连接请求")

    Text {
        text: qsTr("%1 请求远程协助本机").arg(root.peerAlias)
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
                text: qsTr("对方连接码")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontHint
                color: Theme.textHint
            }
            Item {
                Layout.fillWidth: true
            }
            Text {
                // 静态映射条目连接码未交换时兜底提示（连接走 alias 不受阻）
                text: root.peerCode === "" ? qsTr("待连接后交换") : root.peerCode
                font.family: Theme.fontFamily
                font.pixelSize: root.peerCode === "" ? 13 : 18
                font.weight: Font.DemiBold
                font.letterSpacing: 2
                color: root.peerCode === "" ? Theme.textHint : Theme.primary
            }
        }
    }
    Text {
        text: "IP：" + root.peerIp
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontHint
        color: Theme.textSecond  // IP 是安全核对要素，hint 色在毛玻璃底上难辨认
    }

    // 信任记忆复选框（确认后才有记忆缓存）
    CheckBox {
        id: rememberChk
        text: qsTr("记住此设备，下次免确认直连")
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
            text: qsTr("同意")
            onClicked: {
                root.close();
                root.accepted(rememberChk.checked);
            }
        }
    ]
}
