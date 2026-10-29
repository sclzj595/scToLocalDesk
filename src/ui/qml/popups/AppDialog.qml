// ============================================================
// scToLocalDesk | src/ui/qml/popups/AppDialog.qml
// 弹窗基座（封装化/模块化，Doc13 §3）：所有业务弹窗统一从
// 这里长出来——居中/模态/毛玻璃背景/标题/底部按钮行/出入场
// 动画只在此定义一份，实例只声明内容与动作（对齐 Widgets 端
// GlassDialog 门面思想：调用点零样式代码）
// 用法：
//   AppDialog {
//       title: qsTr("标题")
//       width: 400; height: 268
//       Text { ... }                      // 默认属性：内容区（可多个）
//       footerChildren: [ AppButton {...}, AppButton { primary: false; ... } ]
//   }
// 注：底部按钮行与内容区之间由弹性 Item 撑开，内容少时按钮沉底
// ============================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Popup {
    id: root

    /// 内容区子项（默认属性）：声明在实例体里的元素进入内容列
    default property alias content: contentCol.data
    /// 底部按钮行：传 AppButton 数组（右对齐，间距 10）
    property alias footerChildren: footerRow.data
    /// 标题（fontTitle/Medium/textMain）
    property string title: ""
    property Item glassSource

    anchors.centerIn: Overlay.overlay
    modal: true
    closePolicy: Popup.NoAutoClose  // 需 Esc 关闭的实例自行覆盖（如密码弹窗）
    padding: 24

    background: GlobalGlass {
        glassSource: root.glassSource
        radiusPx: Theme.cardRadius
        shadowed: true
    }

    contentItem: ColumnLayout {
        spacing: 10

        Text {
            text: root.title
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Medium
            color: Theme.textMain
        }

        ColumnLayout {
            id: contentCol
            spacing: 10
            Layout.fillWidth: true
        }

        Item {
            Layout.fillHeight: true
        }

        RowLayout {
            id: footerRow
            Layout.alignment: Qt.AlignRight
            spacing: 10
        }
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.durPopup
            easing.type: Theme.easingType
        }
        NumberAnimation {
            property: "scale"
            from: 0.97
            to: 1
            duration: Theme.durPopup
            easing.type: Theme.easingType
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: Theme.durPopup
            easing.type: Theme.easingType
        }
        NumberAnimation {
            property: "scale"
            from: 1
            to: 0.97
            duration: Theme.durPopup
            easing.type: Theme.easingType
        }
    }
}
