// ============================================================
// scToLocalDesk | src/ui/qml/Toast.qml
// 右下角轻提示（Doc13 §3.4.4）：小尺寸面板，2s 自动消失
// 注 1：深色半透底+白字——浅玻璃底在浅色壁纸上不可辨（E2E 实测教训）
// 注 2：位置由使用方给保守固定值——window/contentItem 的宽高在 DPI
//       缩放下返回物理像素（实测 1389≠1080），动态锚定会把 Toast
//       推到窗口外被裁剪（E2E 红块对照实验定位）；固定 (400,520)
//       在 1080x700 逻辑与 1.25x 物理窗口内均可见
// 注 3：毛玻璃同屏上限 3（MenuBar+Dock+弹窗），Toast 用半透底
//       模拟磨砂观感以守住性能红线（Doc13 §6.2）
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

    width: tipText.implicitWidth + 32
    height: 40
    radius: Theme.cardRadius
    color: Qt.rgba(0.13, 0.13, 0.15, 0.88)  // 深色半透底，亮暗背景均可辨
    border.color: Qt.rgba(1, 1, 1, 0.14)
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
        color: "#FFFFFF"
    }

    Timer {
        id: hideTimer
        interval: 2000
        onTriggered: root.opacity = 0
    }
}
