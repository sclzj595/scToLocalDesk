// ============================================================
// scToLocalDesk | src/ui/qml/pages/Page_Settings.qml
// 配置页（Doc13 §四.5 / Doc17 全量配置项）：分组卡片布局
// 卡片纯色底（红线2），仅体现控件规范：Switch/TextField/按钮
// ============================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."
import ScLocaldesk 1.0

Item {
    id: root

    // 7 组字段 ↔ AppConfig（Doc18 §3.3）：初值经 ScLocaldesk.settingsValue 注入，
    // 变更经 settingsSetValue 校验/落盘 config.ini/即时生效项接服务 setter

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: root.width - 56
            x: 28
            y: 24
            spacing: 16

            Text {
                text: qsTr("设置")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Medium
                color: Theme.textMain
            }

            // ---- 通用 ----
            GroupCard {
                title: qsTr("通用")
                TextRow {
                    label: qsTr("本机连接名")
                    value: ScLocaldesk.settingsValue("general.alias")
                    onEdited: v => ScLocaldesk.settingsSetValue("general.alias", v)
                }
                SwitchRow {
                    text: qsTr("开机自动启动")
                    checked: ScLocaldesk.settingsValue("app.auto_start") === "true"
                    onToggled: ScLocaldesk.settingsSetValue("app.auto_start", checked ? "true" : "false")
                }
                SwitchRow {
                    text: qsTr("启动后最小化到托盘")
                    checked: ScLocaldesk.settingsValue("app.minimize_to_tray") === "true"
                    onToggled: ScLocaldesk.settingsSetValue("app.minimize_to_tray", checked ? "true" : "false")
                }
            }

            // ---- 外观 ----
            GroupCard {
                title: qsTr("外观")
                SwitchRow {
                    text: qsTr("深色主题")
                    checked: Theme.isDark
                    onToggled: {
                        Theme.toggleDark();
                        ScLocaldesk.settingsSetValue("appearance.theme", Theme.isDark ? "dark" : "light");
                    }
                }
                SwitchRow {
                    text: qsTr("降低透明度（无障碍）")
                    checked: Theme.reducedTransparency
                    onToggled: {
                        Theme.reducedTransparency = checked;
                        ScLocaldesk.settingsSetValue("appearance.reduce_transparency", checked ? "true" : "false");
                    }
                }
            }

            // ---- 网络 ----
            GroupCard {
                title: qsTr("网络")
                TextRow {
                    label: qsTr("会话端口")
                    value: ScLocaldesk.settingsValue("network.ws_port")
                    numeric: true
                    onEdited: v => ScLocaldesk.settingsSetValue("network.ws_port", v)
                }
                TextRow {
                    label: qsTr("组播地址")
                    value: ScLocaldesk.settingsValue("network.multicast_addr")
                    onEdited: v => ScLocaldesk.settingsSetValue("network.multicast_addr", v)
                }
                SwitchRow {
                    text: qsTr("组播发现（关闭走静态映射，重启生效）")
                    checked: ScLocaldesk.settingsValue("network.multicast_enabled") === "true"
                    onToggled: ScLocaldesk.settingsSetValue("network.multicast_enabled", checked ? "true" : "false")
                }
                TextRow {
                    label: qsTr("静态映射")
                    value: ScLocaldesk.settingsValue("network.static_map")
                    onEdited: v => ScLocaldesk.settingsSetValue("network.static_map", v)
                }
                TextRow {
                    label: qsTr("心跳间隔（秒）")
                    value: ScLocaldesk.settingsValue("network.heartbeat_interval_s")
                    numeric: true
                    onEdited: v => ScLocaldesk.settingsSetValue("network.heartbeat_interval_s", v)
                }
            }

            // ---- 远程会话 ----
            GroupCard {
                title: qsTr("远程会话")
                TextRow {
                    label: qsTr("帧率（fps）")
                    value: ScLocaldesk.settingsValue("session.fps")
                    numeric: true
                    onEdited: v => ScLocaldesk.settingsSetValue("session.fps", v)
                }
                TextRow {
                    label: qsTr("JPEG 画质")
                    value: ScLocaldesk.settingsValue("session.jpeg_quality")
                    numeric: true
                    onEdited: v => ScLocaldesk.settingsSetValue("session.jpeg_quality", v)
                }
            }

            // ---- 文件传输 ----
            GroupCard {
                title: qsTr("文件传输")
                TextRow {
                    label: qsTr("接收目录")
                    value: ScLocaldesk.settingsValue("file.save_path")
                    onEdited: v => ScLocaldesk.settingsSetValue("file.save_path", v)
                }
                TextRow {
                    label: qsTr("分片大小（KB）")
                    value: ScLocaldesk.settingsValue("file.chunk_size_kb")
                    numeric: true
                    onEdited: v => ScLocaldesk.settingsSetValue("file.chunk_size_kb", v)
                }
                SwitchRow {
                    text: qsTr("接收前需确认")
                    checked: ScLocaldesk.settingsValue("file.confirm_before_transfer") === "true"
                    onToggled: ScLocaldesk.settingsSetValue("file.confirm_before_transfer", checked ? "true" : "false")
                }
                SwitchRow {
                    text: qsTr("文件名安全校验（拒收路径穿越等危险名）")
                    checked: ScLocaldesk.settingsValue("file.safe_path") !== "false"
                    onToggled: ScLocaldesk.settingsSetValue("file.safe_path", checked ? "true" : "false")
                }
            }

            // ---- 安全 ----
            GroupCard {
                title: qsTr("安全")
                SwitchRow {
                    text: qsTr("允许无人值守连接")
                    checked: ScLocaldesk.settingsValue("security.unattended_enabled") === "true"
                    onToggled: ScLocaldesk.settingsSetValue("security.unattended_enabled", checked ? "true" : "false")
                }
                TextRow {
                    label: qsTr("无人值守密码")
                    value: ""
                    echo: true
                    onEdited: v => ScLocaldesk.setUnattendedEnabled(
                                  ScLocaldesk.settingsValue("security.unattended_enabled") === "true", v)
                }
            }

            // ---- 日志 ----
            GroupCard {
                title: qsTr("日志")
                TextRow {
                    label: qsTr("日志级别")
                    value: ScLocaldesk.settingsValue("log.level")
                    onEdited: v => ScLocaldesk.settingsSetValue("log.level", v)
                }
                TextRow {
                    label: qsTr("日志上限（MB）")
                    value: ScLocaldesk.settingsValue("log.max_size_mb")
                    numeric: true
                    onEdited: v => ScLocaldesk.settingsSetValue("log.max_size_mb", v)
                }
                TextRow {
                    label: qsTr("日志文件数")
                    value: ScLocaldesk.settingsValue("log.max_files")
                    numeric: true
                    onEdited: v => ScLocaldesk.settingsSetValue("log.max_files", v)
                }
            }
        }
    }

    // ---- 分组卡片（纯色底，Doc13 §3.4.3） ----
    component GroupCard: Rectangle {
        id: cardRoot
        property string title
        default property alias content: col.data

        Layout.fillWidth: true
        height: col.implicitHeight + titleLbl.height + 42
        radius: Theme.cardRadius
        color: Theme.cardBg
        border.color: Theme.cardBorder
        border.width: 1

        ColumnLayout {
            id: col
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10

            Text {
                id: titleLbl
                text: cardRoot.title
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCard
                font.weight: Font.Medium
                color: Theme.textMain
                Layout.bottomMargin: 4
            }
        }
    }

    component SwitchRow: RowLayout {
        id: rowRoot
        property string text
        property alias checked: sw.checked
        signal toggled()

        Layout.fillWidth: true
        Text {
            text: rowRoot.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            color: Theme.textMain
            Layout.fillWidth: true
        }
        Switch {
            id: sw
            // 自定义 indicator 无 text，读屏器靠此名播报（Doc18 §7.3）
            Accessible.name: rowRoot.text
            onToggled: rowRoot.toggled()
            indicator: Rectangle {
                implicitWidth: 42
                implicitHeight: 24
                radius: 12
                color: sw.checked ? Theme.primary : Theme.divider
                Behavior on color {
                    ColorAnimation {
                        duration: Theme.durHover
                    }
                }
                Rectangle {
                    x: sw.checked ? parent.width - width - 3 : 3
                    anchors.verticalCenter: parent.verticalCenter
                    width: 18
                    height: 18
                    radius: 9
                    color: "#FFFFFF"
                    Behavior on x {
                        NumberAnimation {
                            duration: Theme.durHover
                            easing.type: Theme.easingType
                        }
                    }
                }
            }
        }
    }

    component TextRow: RowLayout {
        id: txtRoot
        property string label
        property string value
        property bool echo: false
        property bool numeric: false   // 纯数字输入（端口/帧率/画质等）
        signal edited(string v)

        Layout.fillWidth: true
        Text {
            text: txtRoot.label
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            color: Theme.textMain
            Layout.fillWidth: true
        }
        TextField {
            Layout.preferredWidth: 220
            text: txtRoot.value
            echoMode: txtRoot.echo ? TextInput.Password : TextInput.Normal
            selectByMouse: true
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            color: Theme.textMain
            placeholderTextColor: Theme.textHint
            leftPadding: 12
            rightPadding: 12
            validator: txtRoot.numeric ? numVal : null
            RegularExpressionValidator {
                id: numVal
                regularExpression: /[0-9]{0,5}/
            }
            background: Rectangle {
                radius: Theme.btnRadius
                color: Theme.winBg
                border.color: parent.activeFocus ? Theme.primary : Theme.cardBorder
                border.width: 1
                Behavior on border.color {
                    ColorAnimation {
                        duration: Theme.durHover
                    }
                }
            }
            onEditingFinished: txtRoot.edited(text)
        }
    }
}
