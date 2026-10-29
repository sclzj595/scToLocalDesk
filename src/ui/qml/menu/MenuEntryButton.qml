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

    // MouseArea 独占抓取（红线）：TapHandler 是被动抓取，按下会穿透到
    // MenuBarRow 底层整栏拖拽 MouseArea → startSystemMove 原生拖拽循环
    // 吞掉释放 → tap 永不完成 → 菜单打不开（"点菜单没反应"根因 1）
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            // DropMenu 声明在按钮内部：x/y 相对按钮——直接 (0, h+2)。
            // 此前 mapToItem(null) 得窗口坐标再赋给按钮相对系 → 弹到
            // 屏幕外（根因 2）；open() 即可，无需 Menu.popup()
            menu.x = 0;
            menu.y = root.height + 2;
            menu.open();
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
