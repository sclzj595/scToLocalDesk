// ============================================================
// scToLocalDesk | src/ui/qml/menu/MenuBarRow.qml
// 顶部菜单栏（Doc13 §3.1）：macOS 现代 BS 风格，高 32px
// 三段布局：左 App 标识 / 中下拉菜单 / 右窗口按钮 + 主题切换
// 整栏可拖拽移动无边框窗口（红线4，startSystemMove 不卡顿）
// ============================================================
import QtQuick
import QtQuick.Controls
import ".."
import "."

Item {
    id: root

    property Item glassSource
    signal menuAction(string action)
    signal themeToggled()

    // ---------------- 整栏拖拽（底层 MouseArea，按钮区事件被上层吸收） ----------------
    MouseArea {
        z: 0
        anchors.fill: parent
        onPressed: root.Window.window.startSystemMove()
    }

    // ---------------- 左：App 图标 + 标题 ----------------
    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 12
        spacing: 8
        Rectangle {
            width: 18
            height: 18
            radius: 5
            anchors.verticalCenter: parent.verticalCenter
            gradient: Gradient {
                GradientStop {
                    position: 0
                    color: Theme.primary
                }
                GradientStop {
                    position: 1
                    color: "#7B2CBF"
                }
            }
            Text {
                anchors.centerIn: parent
                text: "S"
                color: "#FFFFFF"
                font.bold: true
                font.pixelSize: 11
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("SC 邻桌")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Medium
            color: Theme.textMain
        }
    }

    // ---------------- 中：五个下拉菜单 ----------------
    Row {
        id: menuRow
        anchors.centerIn: parent
        spacing: 2

        Repeater {
            model: [{
                    "title": qsTr("文件"),
                    "items": [{
                            "text": qsTr("发送文件…"),
                            "action": "file.send"
                        }, {
                            "sep": true
                        }, {
                            "text": qsTr("退出"),
                            "action": "app.quit"
                        }]
                }, {
                    "title": qsTr("连接"),
                    "items": [{
                            "text": qsTr("新建连接…"),
                            "action": "conn.new"
                        }, {
                            "text": qsTr("断开当前会话"),
                            "action": "conn.close"
                        }]
                }, {
                    "title": qsTr("视图"),
                    "items": [{
                            "text": qsTr("刷新设备列表"),
                            "action": "view.refresh"
                        }, {
                            "sep": true
                        }, {
                            "text": Theme.isDark ? qsTr("切至浅色主题") : qsTr("切至深色主题"),
                            "action": "view.theme"
                        }, {
                            "text": qsTr("降低透明度"),
                            "action": "view.reduce"
                        }]
                }, {
                    "title": qsTr("工具"),
                    "items": [{
                            "text": qsTr("设置…"),
                            "action": "tool.settings"
                        }, {
                            "text": qsTr("清除缓存数据"),
                            "action": "tool.clear"
                        }]
                }, {
                    "title": qsTr("帮助"),
                    "items": [{
                            "text": qsTr("关于 SC 邻桌"),
                            "action": "help.about"
                        }]
                }]

            delegate: MenuEntryButton {
                glassSource: root.glassSource
                title: modelData.title
                items: modelData.items
                onMenuAction: action => root.menuAction(action)
            }
        }
    }

    // ---------------- 右：主题切换 + 窗口按钮 ----------------
    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 8
        spacing: 4

        WindowToolButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.isDark ? "☀" : "☾"
            tip: qsTr("切换主题")
            onClicked: root.themeToggled()
        }
        WindowToolButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: "─"
            tip: qsTr("最小化")
            onClicked: root.Window.window.showMinimized()
        }
        WindowToolButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: "□"
            tip: qsTr("最大化")
            onClicked: {
                const w = root.Window.window;
                w.visibility === Window.Maximized ? w.showNormal() : w.showMaximized();
            }
        }
        WindowToolButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: "✕"
            tip: qsTr("关闭")
            onClicked: root.Window.window.close()
        }
    }
}
