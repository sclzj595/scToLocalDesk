// ============================================================
// scToLocalDesk | src/ui/qml/pages/Page_Connect.qml
// 通过连接码连接（向日葵核心交互）：9 位三段输入 + 自动跳格
// + 最近连接列表（信任缓存后免确认）
// ============================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ScLocaldesk
import ".."

Item {
    id: root

    signal connectRequested(string code)

    readonly property var history: [
        {
            name: "设计部-张工",
            code: "482 130 556",
            time: "今天 14:32"
        },
        {
            name: "会议室-终端",
            code: "903 245 117",
            time: "昨天 09:15"
        },
        {
            name: "运维室-李工",
            code: "107 662 384",
            time: "10-19 16:48"
        }
    ]

    function tryConnect() {
        const code = f0.text + f1.text + f2.text;
        if (code.length !== 9) {
            hint.text = "请输入完整 9 位连接码";
            hint.color = Theme.accentRed;
            return;
        }
        root.connectRequested(code.slice(0, 3) + " " + code.slice(3, 6) + " " + code.slice(6, 9));
    }

    // 粘贴 9 位码：拆三段分发；带空格/横线均容忍
    function distributePaste(seg, pasted) {
        const digits = pasted.replace(/[^0-9]/g, "");
        if (digits.length < 2) {
            return false;  // 单个数字走正常输入
        }
        const all = f0.text + f1.text + f2.text;
        const idx = seg.segIndex * 3;
        const merged = (all.slice(0, idx) + digits + all.slice(idx + seg.text.length))
                           .replace(/[^0-9]/g, "").slice(0, 9);
        f0.text = merged.slice(0, 3);
        f1.text = merged.slice(3, 6);
        f2.text = merged.slice(6, 9);
        const filled = f0.text.length + f1.text.length + f2.text.length;
        const target = filled <= 3 ? f0 : filled <= 6 ? f1 : f2;
        target.forceActiveFocus();
        target.cursorPosition = target.text.length;
        if (filled === 9) {
            root.tryConnect();
        }
        return true;
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 14

        Text {
            text: "通过连接码连接"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Medium
            color: Theme.textMain
        }
        Text {
            text: "输入对方首页展示的 9 位连接码，对方确认后建立会话；确认过一次将进入信任缓存"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontHint
            color: Theme.textHint
        }

        RowLayout {
            Layout.topMargin: 12
            spacing: 12

            component CodeSeg: TextField {
                id: seg
                property int segIndex
                property Item prevSeg
                property Item nextSeg

                maximumLength: 3
                // RowLayout 取 implicit 尺寸而非 width（否则空框缩窄、有字撑宽）
                implicitWidth: 96
                implicitHeight: 48
                horizontalAlignment: TextInput.AlignHCenter
                font.family: Theme.fontFamily
                font.pixelSize: 24
                font.weight: Font.DemiBold
                font.letterSpacing: 2
                color: Theme.textMain
                selectByMouse: true
                inputMethodHints: Qt.ImhDigitsOnly
                validator: RegularExpressionValidator {
                    regularExpression: /[0-9]{0,3}/
                }
                background: Rectangle {
                    radius: Theme.btnRadius
                    color: Theme.cardBg
                    border.color: seg.activeFocus ? Theme.primary
                                  : seg.text.length === 3 ? Qt.alpha(Theme.primary, 0.35)
                                  : Theme.divider
                    border.width: 1
                    Behavior on border.color {
                        ColorAnimation {
                            duration: Theme.durHover
                        }
                    }
                }
                // 粘贴拦截：QML TextField 无 pasted 信号，经 bridge 读剪贴板分发
                Keys.onPressed: event => {
                    if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9
                            && text.length === 3 && selectionStart === -1
                            && nextSeg != null) {
                        // 满格续输：数字直接落下一格（不依赖 textChanged 跳焦时序）
                        nextSeg.forceActiveFocus();
                        nextSeg.cursorPosition = nextSeg.text.length;
                        nextSeg.text = (nextSeg.text + event.text).slice(-3);
                        event.accepted = true;
                        return;
                    }
                    if (event.matches(StandardKey.Paste)) {
                        if (root.distributePaste(seg, ScLocaldesk.clipboardText())) {
                            event.accepted = true;  // 分发接管，走默认粘贴会破坏分段
                        }
                        return;
                    }
                    if (event.key === Qt.Key_Backspace) {
                        if (text.length === 0 && prevSeg != null) {
                            prevSeg.forceActiveFocus();
                            prevSeg.selectAll();
                            event.accepted = true;
                        } else if (text.length > 0 && prevSeg != null
                                   && selectionStart === -1 && cursorPosition === 0) {
                            // 光标在本段 0 位：删上一段末位（三段视作连续串）
                            prevSeg.text = prevSeg.text.slice(0, prevSeg.text.length - 1);
                            prevSeg.forceActiveFocus();
                            prevSeg.cursorPosition = prevSeg.text.length;
                            event.accepted = true;
                        }
                    } else if (event.key === Qt.Key_Left && cursorPosition === 0
                               && prevSeg != null) {
                        prevSeg.forceActiveFocus();
                        prevSeg.cursorPosition = prevSeg.text.length;
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Right && cursorPosition === text.length
                               && text.length === 3 && nextSeg != null) {
                        nextSeg.forceActiveFocus();
                        nextSeg.cursorPosition = 0;
                        event.accepted = true;
                    }
                }
                onTextChanged: if (text.length === 3 && nextSeg != null) {
                    nextSeg.forceActiveFocus();
                    nextSeg.selectAll();
                }
                Keys.onReturnPressed: root.tryConnect()
                Keys.onEnterPressed: root.tryConnect()
                onActiveFocusChanged: if (activeFocus) {
                    selectAll();  // 点击/Tab 进入选中态，直接覆盖输入
                }
            }

            CodeSeg {
                id: f0
                segIndex: 0
                nextSeg: f1
                Layout.alignment: Qt.AlignVCenter
            }
            Text {
                text: "–"
                font.pixelSize: 18
                color: Theme.textHint
            }
            CodeSeg {
                id: f1
                segIndex: 1
                prevSeg: f0
                nextSeg: f2
                Layout.alignment: Qt.AlignVCenter
            }
            Text {
                text: "–"
                font.pixelSize: 18
                color: Theme.textHint
            }
            CodeSeg {
                id: f2
                segIndex: 2
                prevSeg: f1
                Layout.alignment: Qt.AlignVCenter
            }

            Item {
                Layout.preferredWidth: 8
            }

            Button {
                text: "连 接"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCard
                onClicked: root.tryConnect()
                contentItem: Text {
                    text: parent.text
                    font: parent.font
                    color: "#FFFFFF"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.btnRadius
                    color: parent.hovered ? Theme.primaryHover : Theme.primary
                    scale: parent.pressed ? 0.97 : 1.0
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
        }

        Text {
            id: hint
            text: " "
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontHint
            color: Theme.textHint
        }

        RowLayout {
            Layout.topMargin: 16
            Text {
                text: "最近连接"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCard
                font.weight: Font.Medium
                color: Theme.textMain
            }
            Item {
                Layout.fillWidth: true
            }
            Text {
                text: "已信任设备免对方确认"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontHint
                color: Theme.textHint
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10
            model: root.history

            delegate: Rectangle {
                width: ListView.view.width
                height: 64
                radius: Theme.cardRadius
                color: Theme.cardBg
                border.color: hma.containsMouse ? Theme.primary : Theme.cardBorder
                border.width: 1
                Behavior on border.color {
                    ColorAnimation {
                        duration: Theme.durHover
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 14

                    Text {
                        text: modelData.name
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCard
                        font.weight: Font.Medium
                        color: Theme.textMain
                    }
                    Text {
                        text: modelData.code
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        font.letterSpacing: 1
                        color: Theme.primary
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    Text {
                        text: modelData.time
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontHint
                        color: Theme.textHint
                    }
                    Text {
                        text: "连接"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        color: Theme.primary
                    }
                }

                MouseArea {
                    id: hma
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.connectRequested(modelData.code)
                }
            }
        }
    }

    Component.onCompleted: f0.forceActiveFocus()
}
