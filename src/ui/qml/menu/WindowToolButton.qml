// ============================================================
// scToLocalDesk | src/ui/qml/menu/WindowToolButton.qml
// 菜单栏右侧窗口工具按钮：hover 淡入高亮 120ms（Doc13 §1.3.2）
// ============================================================
import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root

    property string glyph
    property string tip
    signal clicked()

    width: 30
    height: 24

    Rectangle {
        id: hl
        anchors.fill: parent
        radius: Theme.menuRadius
        color: root.glyph === "✕" ? Qt.rgba(0.97, 0.15, 0.52, 0.85) : Theme.hoverBg
        opacity: 0
        Behavior on opacity {
            NumberAnimation {
                duration: Theme.durHover
                easing.type: Theme.easingType
            }
        }
    }

    Text {
        anchors.centerIn: parent
        text: root.glyph
        font.pixelSize: 12
        color: hl.opacity > 0 && root.glyph === "✕" ? "#FFFFFF" : Theme.textSecond
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
        onContainsMouseChanged: hl.opacity = containsMouse ? 1 : 0
    }

    ToolTip.visible: ma.containsMouse
    ToolTip.delay: 600
    ToolTip.text: root.tip
}
