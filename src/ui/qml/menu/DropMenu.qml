// ============================================================
// scToLocalDesk | src/ui/qml/menu/DropMenu.qml
// 下拉弹出菜单（Doc13 §3.1）：18px 毛玻璃 + 细描边 + 弱阴影
// 弹出动画：透明度 0→1 + 缩放 0.97→1，120ms（Doc13 §3.1.4）
// 条目高度 28px，hover 半透高亮，圆角 6px，分割线 1px
// ============================================================
import QtQuick
import QtQuick.Controls
import ".."

Popup {
    id: root

    property Item glassSource
    /// 菜单数据：[{ text: "条目", action: "act_id" }] 或 { sep: true } 分隔线
    property alias model: repeater.model
    signal menuAction(string action)

    width: 176
    padding: 6
    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: GlobalGlass {
        glassSource: root.glassSource
        radiusPx: Theme.cardRadius
        shadowed: true
    }

    contentItem: Column {
        spacing: 1
        Repeater {
            id: repeater
            delegate: Loader {
                required property var modelData
                sourceComponent: modelData.sep !== undefined ? sepComp : itemComp
            }
        }
    }

    Component {
        id: sepComp
        Rectangle {
            width: root.width - 12
            height: 1
            color: Theme.divider
        }
    }

    Component {
        id: itemComp
        Item {
            id: entry
            width: root.width - 12
            height: 28

            Rectangle {
                id: hl
                anchors.fill: parent
                radius: Theme.menuRadius
                color: Theme.hoverBg
                opacity: 0
                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.durHover
                        easing.type: Theme.easingType
                    }
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 10
                text: modelData.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                color: Theme.textMain
            }
            MouseArea {
                id: ma
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    root.menuAction(modelData.action);
                    root.close();
                }
            }
            states: State {
                when: ma.containsMouse
                PropertyChanges {
                    target: hl
                    opacity: 1
                }
            }
        }
    }

    // 弹出 / 收起动画（透明度 + 缩放）
    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.durHover
            easing.type: Theme.easingType
        }
        NumberAnimation {
            property: "scale"
            from: 0.97
            to: 1
            duration: Theme.durHover
            easing.type: Theme.easingType
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: Theme.durHover
            easing.type: Theme.easingType
        }
        NumberAnimation {
            property: "scale"
            from: 1
            to: 0.97
            duration: Theme.durHover
            easing.type: Theme.easingType
        }
    }
}
