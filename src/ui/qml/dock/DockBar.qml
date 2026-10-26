// ============================================================
// scToLocalDesk | src/ui/qml/dock/DockBar.qml
// 左侧 Dock 程序坞（Doc13 §3.2）：宽 72px，整栏 18px 毛玻璃
// 图标 32px，仅 hover 圆形半透高亮；点击切换主内容区页面
// 选中项左缘主色竖线标记（Doc13 §8 列表项规则）
// ============================================================
import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root

    property Item glassSource
    property int currentIndex: 0
    signal pageRequested(int index)

    // 条目与 MainWindow pageStack 顺序严格对齐：
    // 0 设备列表 / 1 新建连接 / 2 远程画面 / 3 文件传输 / 4 设置
    readonly property var entries: [{
            "glyph": "◱",
            "tip": qsTr("设备列表")
        }, {
            "glyph": "⧉",
            "tip": qsTr("新建连接")
        }, {
            "glyph": "▣",
            "tip": qsTr("远程画面")
        }, {
            "glyph": "⇅",
            "tip": qsTr("文件传输")
        }, {
            "glyph": "⚙",
            "tip": qsTr("设置")
        }]

    GlobalGlass {
        anchors.fill: parent
        glassSource: root.glassSource
        radiusPx: 0
    }

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 52
        spacing: 10

        Repeater {
            model: root.entries

            delegate: Item {
                id: dockEntry
                width: 56
                height: 56
                // 自绘控件无内置 text，屏幕阅读器依赖显式声明（Doc18 §7.3）
                Accessible.role: Accessible.Button
                Accessible.name: modelData.tip
                Accessible.focusable: true

                // 选中标记：左缘主色竖线
                Rectangle {
                    anchors.left: parent.left
                    anchors.leftMargin: 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: 3
                    height: 20
                    radius: 2
                    color: Theme.primary
                    visible: root.currentIndex === index
                    Behavior on opacity {
                        NumberAnimation {
                            duration: Theme.durHover
                            easing.type: Theme.easingType
                        }
                    }
                }

                // hover 圆形半透高亮
                Rectangle {
                    id: circle
                    anchors.centerIn: parent
                    width: 46
                    height: 46
                    radius: 23
                    color: Theme.hoverBg
                    opacity: 0
                    scale: 0.9
                    Behavior on opacity {
                        NumberAnimation {
                            duration: Theme.durHover
                            easing.type: Theme.easingType
                        }
                    }
                    Behavior on scale {
                        NumberAnimation {
                            duration: Theme.durHover
                            easing.type: Theme.easingType
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    text: modelData.glyph
                    font.pixelSize: 26
                    color: root.currentIndex === index ? Theme.primary : Theme.textSecond
                    Behavior on color {
                        ColorAnimation {
                            duration: Theme.durHover
                        }
                    }
                }

                MouseArea {
                    id: ma
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.currentIndex = index;
                        root.pageRequested(index);
                    }
                    onContainsMouseChanged: {
                        circle.opacity = containsMouse ? 1 : 0;
                        circle.scale = containsMouse ? 1.0 : 0.9;
                    }
                }

                ToolTip.visible: ma.containsMouse
                ToolTip.delay: 500
                ToolTip.text: modelData.tip
            }
        }
    }
}
