// ============================================================
// scToLocalDesk | src/ui/qml/menu/MenuEntryButton.qml
// 菜单栏文字项（Doc13 §3.1.2）：13px 常规字重，hover 半透高亮
// 圆角 6px，动画 120ms；点击弹出同材质玻璃下拉菜单
// ============================================================
import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root

    property Item glassSource
    property string title
    property var items
    signal menuAction(string action)

    width: label.implicitWidth + 20
    height: Theme.menuBarHeight - 6

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
        id: label
        anchors.centerIn: parent
        text: root.title
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        color: Theme.textMain
    }

    TapHandler {
        onTapped: {
            const p = root.mapToItem(null, 0, root.height + 2);
            menu.popup();
            menu.x = p.x;
            menu.y = p.y;
        }
    }
    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    DropMenu {
        id: menu
        glassSource: root.glassSource
        model: root.items
        onMenuAction: action => root.menuAction(action)
        onAboutToShow: hl.opacity = 1
        onAboutToHide: if (!hover.hovered)
            hl.opacity = 0
    }

    HoverHandler {
        id: hover
        onHoveredChanged: hl.opacity = hovered || menu.opened ? 1 : 0
    }
}
