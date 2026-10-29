// ============================================================
// scToLocalDesk | src/ui/qml/Theme.qml
// 全局主题单例（Doc13 §2/§7）：双主题色板 + 固定参数 + 动效时长
// 红线：blur=18 不可改；QML 禁止硬编码色值，一律绑定本单例
// ============================================================
pragma Singleton
import QtQuick

QtObject {
    id: theme

    // ---------------- 主题状态（持久化走 config.ini，Doc17） ----------------
    property bool isDark: false
    property bool reducedTransparency: false  // 无障碍：降低透明度（Doc13 §1.3）

    // ---------------- 固定参数（Doc13 §1.2，不可修改） ----------------
    readonly property int glassBlur: 18          // 毛玻璃模糊半径，锁定 18px
    readonly property real glassFillLight: 0.18  // 浅色玻璃填充不透明度
    readonly property real glassFillDark: 0.22   // 深色玻璃填充不透明度
    readonly property int winRadius: 20          // 主窗口圆角
    readonly property int panelRadius: 16        // 大面板圆角
    readonly property int cardRadius: 12         // 卡片/弹窗圆角
    readonly property int btnRadius: 8           // 按钮/输入框圆角
    readonly property int menuRadius: 6          // 菜单条目圆角
    readonly property int menuBarHeight: 32      // 顶部菜单栏高度
    readonly property int dockWidth: 72          // 左侧 Dock 宽度

    // ---------------- 动效（Doc13 §6，Easing.OutCubic 统一） ----------------
    readonly property int durHover: 120
    readonly property int durPopup: 180
    readonly property int durPage: 220
    readonly property int easingType: Easing.OutCubic

    // ---------------- 字体排版（Doc13 §5） ----------------
    readonly property string fontFamily: Qt.platform.os === "windows" ? "Microsoft YaHei"
                                       : Qt.platform.os === "osx" ? "San Francisco"
                                       : "Noto Sans"
    readonly property int fontTitle: 16   // 窗口标题 Medium
    readonly property int fontCard: 14    // 卡片标题 Medium
    readonly property int fontBody: 13    // 菜单/正文
    readonly property int fontHint: 12    // 辅助说明

    // ---------------- 主色调（Doc13 §2.1 粉紫主题） ----------------
    readonly property color primary: "#9D4EDD"
    readonly property color primaryHover: Qt.lighter(primary, 1.10)
    readonly property color primaryDisabled: Qt.alpha(primary, 0.45)
    readonly property color accentBlue: "#3A86FF"   // 连接成功
    readonly property color accentRed: "#F72585"    // 断开/拒绝
    readonly property color accentGreen: "#4CC9F0"  // 在线空闲

    // ---------------- 双主题基础色（Doc13 §2.2） ----------------
    readonly property color winBg: isDark ? "#15171C" : "#F7F9FC"
    readonly property color glassBg: isDark ? Qt.rgba(1, 1, 1, 0.22)
                                            : Qt.rgba(1, 1, 1, 0.18)
    readonly property color glassBorder: isDark ? Qt.rgba(1, 1, 1, 0.35)
                                                : Qt.rgba(1, 1, 1, 0.75)
    readonly property color textMain: isDark ? "#F2F3F5" : "#1D2129"
    readonly property color textSecond: isDark ? "#909399" : "#6E7681"
    readonly property color textHint: isDark ? "#6B7280" : "#A0A8B3"
    readonly property color divider: isDark ? Qt.rgba(1, 1, 1, 0.10)
                                            : Qt.rgba(0, 0, 0, 0.08)
    readonly property color hoverBg: isDark ? Qt.rgba(1, 1, 1, 0.12)
                                            : Qt.rgba(0, 0, 0, 0.06)
    readonly property color cardBg: isDark ? "#1E2128" : "#FFFFFF"
    readonly property color cardBorder: isDark ? Qt.rgba(1, 1, 1, 0.08)
                                               : Qt.rgba(0, 0, 0, 0.06)
    // 降透明度模式：玻璃退化为纯色半透底（Doc13 §6.2）
    readonly property color glassSolid: isDark ? Qt.rgba(0.10, 0.11, 0.13, 0.85)
                                               : Qt.rgba(1, 1, 1, 0.85)

    function toggleDark() {
        isDark = !isDark;
        // C++ 侧 GlassTheme 同步（托盘菜单取色源，qt_tray 双端共用）
        ScLocaldesk.setUiTheme(isDark);
        // TODO(V1.0): 写回 config.ini appearance.theme（Doc17 §3.1）
    }
}
