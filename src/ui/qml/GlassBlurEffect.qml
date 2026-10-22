// ============================================================
// scToLocalDesk | src/ui/qml/GlassBlurEffect.qml
// 毛玻璃增强层（Qt 6.5+ 专用）：18px 真模糊 + 弱阴影
// 仅由 GlobalGlass.qml 通过 Loader 按需加载（Qt 6.2 无
// QtQuick.Effects 模块，直接 import 会使文件加载失败，故全部
// Effects 用法隔离在本组件内）
// 红线1：blurMax 锁 18px，严禁改亚克力动态模糊
// ============================================================
import QtQuick
import QtQuick.Effects
import "."

Item {
    id: root

    /// 被模糊的背景层（内容层根）；玻璃面板与其必须为兄弟层级
    required property Item glassSource
    /// 弱阴影开关（悬浮弹窗用）
    property bool shadowed: false
    /// 主题值由 GlobalGlass 注入（递归 import 下 singleton 不可靠，
    /// 本组件禁止引用 Theme）
    property int blurMaxPx: 18
    property bool dark: false
    /// 阴影形状圆角（与面板圆角一致）
    property real radiusPx: 0

    // 面板在源坐标系中的位置（驱动 sourceRect 跟随移动）
    readonly property point srcPos: glassSource ? mapToItem(glassSource, 0, 0)
                                                : Qt.point(0, 0)

    // ---------- 18px 背景模糊（核心） ----------
    ShaderEffectSource {
        id: grab
        anchors.fill: parent
        visible: false
        sourceItem: root.glassSource
        // 源区域跟随面板位置/尺寸；半分辨率采样提速（模糊低频，无观感损失）
        sourceRect: Qt.rect(root.srcPos.x, root.srcPos.y, root.width, root.height)
        textureSize: Qt.size(Math.max(1, root.width * 0.5),
                             Math.max(1, root.height * 0.5))
        live: true
        smooth: true
        recursive: false
    }

    MultiEffect {
        anchors.fill: parent
        source: grab
        blurEnabled: true
        blurMax: root.blurMaxPx   // 红线：18px 固定（由 Theme 注入）
        blur: 1.0
        saturation: 0.15
        brightness: root.dark ? 0.92 : 1.0
    }

    // ---------- 弱阴影（垂直 4px / 模糊 16px / 12%，Doc13 §1.2） ----------
    // 阴影源为隐藏形状层（visible:false 仍可作纹理），避免抓自身自反馈
    Rectangle {
        id: shadowShape
        visible: false
        width: root.width
        height: root.height
        radius: root.radiusPx
        color: "#000000"
    }

    MultiEffect {
        anchors.fill: parent
        source: shadowShape
        visible: root.shadowed
        autoPaddingEnabled: false
        shadowEnabled: root.shadowed
        shadowBlur: 0.5
        shadowVerticalOffset: 4
        shadowColor: Qt.rgba(0, 0, 0, 0.12)
    }
}
