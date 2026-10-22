// ============================================================
// scToLocalDesk | src/ui/qml/pages/Page_DeviceList.qml
// 局域网设备列表（向日葵模式）：纯色内容区，禁玻璃（红线2）
// 顶部：本机信息卡（连接名=username@主机 + 9 位连接码 + 复制）
// 设备卡：连接名 / 连接码 / IP / 状态点 / 信任角标
// 交互：未信任点击 → 连接确认弹窗（勾选记忆后进信任缓存）；
//       已信任 → 直连。hover 规范（Doc13）：主色实描边 + 外圈柔光晕
//       + 卡面微亮，全部走透明度/颜色动画，禁 scale/translate
//       （transform 会边框错帧残影、文字栅格化发虚）
// V1.0 TODO：连接码由 IdService 按机器指纹派生；信任缓存落 TrustStore
// ============================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Item {
    id: root

    /// trusted=false 时由 MainWindow 弹确认；true 直连
    signal connectRequested(string aliasName, string code, bool trusted)

    // 演示数据（V1.0 由 bridge.selfAlias / IdService / TrustStore 驱动）
    readonly property string selfName: "zhangsan@SC-DESK"
    readonly property string selfCode: "538 194 207"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 18

        // ---------------- 本机信息卡（连接码入口） ----------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            radius: Theme.cardRadius
            color: Theme.cardBg
            border.color: Theme.cardBorder
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 18

                Rectangle {
                    width: 56
                    height: 56
                    radius: 28
                    color: Qt.alpha(Theme.primary, 0.14)
                    Text {
                        anchors.centerIn: parent
                        text: "本"
                        color: Theme.primary
                        font.pixelSize: 20
                        font.weight: Font.Medium
                    }
                }

                ColumnLayout {
                    spacing: 4
                    Text {
                        text: "本机连接名"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontHint
                        color: Theme.textHint
                    }
                    Text {
                        text: root.selfName
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCard
                        font.weight: Font.Medium
                        color: Theme.textMain
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                ColumnLayout {
                    spacing: 4
                    Text {
                        text: "本机连接码"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontHint
                        color: Theme.textHint
                    }
                    Text {
                        text: root.selfCode
                        font.family: Theme.fontFamily
                        font.pixelSize: 26
                        font.weight: Font.DemiBold
                        font.letterSpacing: 3
                        color: Theme.primary
                    }
                }

                Button {
                    id: copyBtn
                    property bool copied: false
                    text: copied ? "已复制 ✓" : "复制"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    onClicked: {
                        // 三段码去空格：split/join 全量去（replace 只换首个）
                        ScLocaldesk.copyText(root.selfCode.split(" ").join(""));
                        copyBtn.copied = true;
                        copyRevert.restart();
                    }
                    Timer {
                        id: copyRevert
                        interval: 1600
                        onTriggered: copyBtn.copied = false
                    }
                    contentItem: Text {
                        text: copyBtn.text
                        font: copyBtn.font
                        color: copyBtn.copied ? Theme.accentGreen : Theme.primary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Theme.btnRadius
                        color: parent.hovered ? Qt.alpha(Theme.primary, 0.10) : "transparent"
                        border.color: Qt.alpha(Theme.primary, 0.45)
                        border.width: 1
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

        // ---------------- 设备网格 ----------------
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "局域网设备"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Medium
                color: Theme.textMain
            }
            Item {
                Layout.fillWidth: true
            }
            Text {
                text: "首次连接需对方确认，确认后免验证直连"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontHint
                color: Theme.textHint
            }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            cellWidth: 292
            cellHeight: 108
            clip: true
            model: devices

            delegate: Item {
                id: cardWrap
                width: grid.cellWidth - 16
                height: grid.cellHeight - 16

                // 外圈柔光晕：悬浮时淡入的主色光环（纯透明度动画，
                // 无 transform —— 从根源避免边框错帧/文字发虚）
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -3
                    radius: Theme.cardRadius + 3
                    color: Qt.alpha(Theme.primary, ma.pressed ? 0.18 : 0.10)
                    opacity: ma.containsMouse ? 1 : 0
                    Behavior on opacity {
                        NumberAnimation {
                            duration: Theme.durHover
                            easing.type: Theme.easingType
                        }
                    }
                }

                Rectangle {
                    id: card
                    anchors.fill: parent
                    radius: Theme.cardRadius
                    color: Theme.cardBg
                    // hover：主色实描边（不再用半透明淡紫）
                    border.color: ma.containsMouse ? Theme.primary : Theme.cardBorder
                    border.width: 1
                    Behavior on border.color {
                        ColorAnimation {
                            duration: Theme.durHover
                        }
                    }

                    // 卡面微亮层（hoverBg 半透叠加，明暗主题自适应）
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.cardRadius
                        color: Theme.hoverBg
                        opacity: ma.containsMouse ? 1 : 0
                        Behavior on opacity {
                            NumberAnimation {
                                duration: Theme.durHover
                                easing.type: Theme.easingType
                            }
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 6

                        RowLayout {
                            spacing: 12
                            Rectangle {
                                width: 40
                                height: 40
                                radius: 20
                                color: Qt.alpha(Theme.primary, 0.14)
                                Text {
                                    anchors.centerIn: parent
                                    text: model.name.charAt(0)
                                    font.pixelSize: 16
                                    font.weight: Font.Medium
                                    color: Theme.primary
                                }
                            }
                            ColumnLayout {
                                spacing: 2
                                Text {
                                    text: model.name
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontCard
                                    font.weight: Font.Medium
                                    color: Theme.textMain
                                }
                                Text {
                                    text: model.user
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontHint
                                    color: Theme.textHint
                                }
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            // 信任角标（向日葵模式：确认后才有记忆缓存）
                            Label {
                                text: model.trusted ? "已信任" : "待确认"
                                font.family: Theme.fontFamily
                                font.pixelSize: 11
                                color: model.trusted ? Theme.accentGreen : Theme.textHint
                                padding: 4
                                background: Rectangle {
                                    radius: 4
                                    color: model.trusted ? Qt.alpha(Theme.accentGreen, 0.12)
                                                         : Qt.alpha(Theme.textHint, 0.10)
                                }
                            }
                        }

                        RowLayout {
                            spacing: 6
                            Text {
                                text: model.code
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                font.letterSpacing: 1
                                font.weight: Font.Medium
                                color: Theme.primary
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                width: 8
                                height: 8
                                radius: 4
                                color: model.state === 0 ? Theme.accentBlue
                                       : model.state === 1 ? Theme.accentGreen
                                       : Theme.accentRed
                            }
                            Text {
                                text: model.ip
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontHint
                                color: Theme.textSecond
                            }
                        }
                    }
                }

                MouseArea {
                    id: ma
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.connectRequested(model.name, model.code, model.trusted)
                }
            }
        }
    }

    ListModel {
        id: devices
        ListElement {
            name: "设计部-张工"
            user: "zhang.s@DESIGN-01"
            code: "482 130 556"
            ip: "192.168.1.23"
            state: 0
            trusted: true
        }
        ListElement {
            name: "运维室-李工"
            user: "li.y@OPS-PC"
            code: "107 662 384"
            ip: "192.168.1.45"
            state: 1
            trusted: false
        }
        ListElement {
            name: "会议室-终端"
            user: "meeting@CONF-TV"
            code: "903 245 117"
            ip: "192.168.1.88"
            state: 2
            trusted: true
        }
    }
}
