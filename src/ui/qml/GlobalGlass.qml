// ============================================================
// scToLocalDesk | src/ui/qml/GlobalGlass.qml
// iOS16 复古毛玻璃封装（Doc13 Skill1）：固定 18px 背景模糊
// 红线1：blurRadius 锁 18，严禁改亚克力动态模糊
// 红线5：1px 半透描边；弹窗模式带弱阴影（y+4 / blur16 / 12%）
// 双路径（Doc13 §13.2）：Qt 6.5+ 加载 GlassBlurEffect（真模糊+弱阴影）；
// Qt 6.2（远程 Linux，无 QtQuick.Effects 模块）Loader 不激活，
// 自动退化为纯色半透底 —— 本文件严禁 import QtQuick.Effects
// glassSource 必须指向内容层根 Item（玻璃面板与源必须为兄弟层级，
// 严禁把玻璃面板放进 source 内部，否则自反馈闪烁）
// ============================================================
import QtQuick
import ScLocaldesk 1.0

Item {
    id: root

    /// 被模糊的背景层（内容层根）；玻璃面板与其为兄弟层级
    required property Item glassSource
    /// 面板圆角（默认卡片 12px）
    property real radiusPx: Theme.cardRadius
    /// 弱阴影（仅悬浮弹窗开启，Doc13 §1.2；仅 6.5+ 生效）
    property bool shadowed: false

    default property alias content: contentHost.data

    // ---------------- 模糊/阴影层：6.5+ 真效果 / 6.2 退化半透底 ----------------
    // 源必须为窗口根（shell）：面板相对根坐标恒为正；若以内容层为源，
    // 菜单栏/Dock 映射出负值 sourceRect → 越界采样输出白色垃圾纹理
    Loader {
        anchors.fill: parent
        active: ScLocaldesk.hasGlassBlur && !Theme.reducedTransparency
        sourceComponent: blurComp
    }

    Component {
        id: blurComp
        GlassBlurEffect {
            glassSource: root.glassSource
            shadowed: root.shadowed
            blurMaxPx: Theme.glassBlur
            dark: Theme.isDark
            radiusPx: root.radiusPx
        }
    }

    // ---------------- 玻璃填充 + 1px 半透描边（红线5） ----------------
    Rectangle {
        anchors.fill: parent
        radius: root.radiusPx
        color: Theme.reducedTransparency || !ScLocaldesk.hasGlassBlur
               ? Theme.glassSolid : Theme.glassBg
        border.color: Theme.glassBorder
        border.width: 1
    }

    // ---------------- 内容宿主 ----------------
    Item {
        id: contentHost
        anchors.fill: parent
    }
}
