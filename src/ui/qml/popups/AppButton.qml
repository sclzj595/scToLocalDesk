// ============================================================
// scToLocalDesk | src/ui/qml/popups/AppButton.qml
// 弹窗按钮基件：primary=主操作（紫底白字），secondary=次操作
// （卡底描边）。按下缩放 0.97（80ms）+ hover 色过渡（durHover）
// 与禁用态（内容 0.45 透明 / 主底 primaryDisabled）统一定义，
// 弹窗实例零按钮样式代码（对齐 Widgets 端 objectName 按钮令牌）
// ============================================================
import QtQuick
import QtQuick.Controls
import ".."

Button {
    id: root

    /// true=主操作（紫底白字，默认）；false=次操作（卡底+divider 描边）
    property bool primary: true

    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody

    contentItem: Text {
        text: root.text
        font: root.font
        color: root.primary ? "#FFFFFF" : Theme.textSecond
        opacity: root.enabled ? 1.0 : 0.45
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: Theme.btnRadius
        color: {
            if (root.primary) {
                if (!root.enabled) return Theme.primaryDisabled;
                return root.hovered ? Theme.primaryHover : Theme.primary;
            }
            return Theme.cardBg;
        }
        border.color: root.primary ? "transparent" : Theme.divider
        border.width: root.primary ? 0 : 1
        scale: root.pressed ? 0.97 : 1.0
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
