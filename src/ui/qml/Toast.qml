// ============================================================
// scToLocalDesk | src/ui/qml/Toast.qml
// 右下角轻提示（Doc13 §3.4.4）：小尺寸玻璃面板，2s 自动消失
// 注：毛玻璃同屏上限 3（MenuBar+Dock+弹窗），Toast 用半透底
// 模拟磨砂观感以守住性能红线（Doc13 §6.2）
// ============================================================
import QtQuick
import "."

Rectangle {
    id: root

    function show(text) {
        tipText.text = text;
        root.opacity = 1;
        hideTimer.restart();
    }

    anchors.right: parent.right
    anchors.bottom: parent.bottom
    anchors.rightMargin: 24
    anchors.bottomMargin: 24
    width: tipText.implicitWidth + 32
    height: 40
    radius: Theme.cardRadius
    color: Theme.glassSolid
    border.color: Theme.glassBorder
    border.width: 1
    opacity: 0
    z: 99

    Behavior on opacity {
        NumberAnimation {
            duration: Theme.durPopup
            easing.type: Theme.easingType
        }
    }

    Text {
        id: tipText
        anchors.centerIn: parent
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        color: Theme.textMain
    }

    Timer {
        id: hideTimer
        interval: 2000
        onTriggered: root.opacity = 0
    }
}
