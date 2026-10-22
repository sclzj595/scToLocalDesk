// ============================================================
// scToLocalDesk | src/ui/qml/pages/Page_FileTransfer.qml
// 双向文件传输列表页（Doc13 §四.4）：纯色内容区
// 进度条主色圆角；完成/失败状态色（Doc13 §2.1 辅助色）
// ============================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Item {
    id: root

    ListModel {
        id: transfers
        ListElement {
            name: "巡检报告-0928.docx"
            dir: 0   // 0 发送 1 接收
            progress: 0.68
            speed: "3.2 MB/s"
            done: 0  // 0 进行中 1 完成 2 失败
        }
        ListElement {
            name: "驱动包_v2.3.zip"
            dir: 1
            progress: 1.0
            speed: ""
            done: 1
        }
        ListElement {
            name: "拓扑图.png"
            dir: 0
            progress: 0.24
            speed: ""
            done: 2
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "文件传输"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Medium
                color: Theme.textMain
            }
            Item {
                Layout.fillWidth: true
            }
            Button {
                text: "发送文件…"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                onClicked: console.log("TODO(V1.0): QFileDialog 选文件 → files.send_file")
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

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10
            model: transfers

            delegate: Rectangle {
                width: ListView.view.width
                height: 64
                radius: Theme.cardRadius
                color: Theme.cardBg
                border.color: Theme.cardBorder
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 14

                    Text {
                        text: model.dir === 0 ? "↑" : "↓"
                        font.pixelSize: 18
                        color: model.dir === 0 ? Theme.primary : Theme.accentBlue
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: model.name
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                color: Theme.textMain
                                elide: Text.ElideRight
                                Layout.maximumWidth: 320
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            Text {
                                text: model.done === 1 ? "已完成"
                                      : model.done === 2 ? "失败"
                                      : model.speed
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontHint
                                color: model.done === 1 ? Theme.accentGreen
                                       : model.done === 2 ? Theme.accentRed
                                       : Theme.textSecond
                            }
                        }
                        ProgressBar {
                            Layout.fillWidth: true
                            from: 0
                            to: 1
                            value: model.progress
                            background: Rectangle {
                                implicitHeight: 6
                                radius: 3
                                color: Theme.divider
                            }
                            contentItem: Item {
                                implicitHeight: 6
                                Rectangle {
                                    width: parent.width * model.progress
                                    height: 6
                                    radius: 3
                                    color: model.done === 2 ? Theme.accentRed : Theme.primary
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
