// ============================================================
// scToLocalDesk | src/ui/qml/pages/Page_FileTransfer.qml
// 双向文件传输列表页（Doc13 §四.4）：纯色内容区
// P3（Doc18 §5）：接 FileService 真事件流（进度/完成/失败），
// 发送入口 = 按钮选文件 + 拖拽入区；进度条主色圆角
// ============================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ".."

Item {
    id: root

    signal requestToast(string text)

    // 真数据源（Evt_File* → bridge 信号）：fileId 唯一索引
    ListModel {
        id: transfers
    }

    function upsert(fileId, name, dir) {
        for (let i = 0; i < transfers.count; ++i) {
            if (transfers.get(i).fileId === fileId) {
                return i;
            }
        }
        transfers.append({fileId: fileId, name: name, dir: dir,
                          progress: 0, done: 0, speed: "",
                          speedVal: 0, lastX: 0, lastT: 0});
        return transfers.count - 1;
    }

    function setDone(fileId, failed) {
        for (let i = 0; i < transfers.count; ++i) {
            if (transfers.get(i).fileId === fileId) {
                transfers.setProperty(i, "done", failed ? 2 : 1);
                transfers.setProperty(i, "progress", failed ? transfers.get(i).progress : 1);
                return;
            }
        }
    }

    // Url（file:///G:/a/b.txt）→ 本地绝对路径
    function toLocalPath(url) {
        return decodeURIComponent(url.toString().replace(/^file:\/\/\//, ""));
    }

    function sendFiles(urls) {
        if (!ScLocaldesk.sessionRunning()) {
            requestToast("当前无进行中的会话，请先连接设备");
            return;
        }
        for (let i = 0; i < urls.length; ++i) {
            if (!ScLocaldesk.sendFile(toLocalPath(urls[i]))) {
                requestToast("发送失败：" + toLocalPath(urls[i]));
            }
        }
    }

    FileDialog {
        id: fileDialog
        fileMode: FileDialog.OpenFiles
        title: "选择要发送的文件"
        onAccepted: root.sendFiles(selectedFiles)
    }

    Connections {
        target: ScLocaldesk
        function onFileProgress(fileId, name, transferred, total, dir) {
            const i = root.upsert(fileId, name, dir);
            transfers.setProperty(i, "progress", total > 0 ? transferred / total : 0);
            // 速度 = 两次进度差 / 时间差（UI 层算，指数平滑抖动收敛）
            const now = Date.now();
            const e = transfers.get(i);
            if (e.lastT > 0 && now > e.lastT) {
                const bps = (transferred - e.lastX) * 1000 / (now - e.lastT);
                const smooth = e.speedVal > 0 ? e.speedVal * 0.6 + bps * 0.4 : bps;
                transfers.setProperty(i, "speedVal", smooth);
                transfers.setProperty(i, "speed", humanSpeed(smooth));
            }
            transfers.setProperty(i, "lastX", transferred);
            transfers.setProperty(i, "lastT", now);
        }
        function humanSpeed(bps) {
            if (bps >= 1073741824) return (bps / 1073741824).toFixed(1) + " GB/s";
            if (bps >= 1048576) return (bps / 1048576).toFixed(1) + " MB/s";
            if (bps >= 1024) return (bps / 1024).toFixed(1) + " KB/s";
            return Math.round(bps) + " B/s";
        }
        function onFileDone(fileId, name, dir) {
            root.setDone(fileId, false);
        }
        function onFileFailed(fileId, name, dir) {
            root.setDone(fileId, true);
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "文件传输"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Medium
                color: Theme.textMain
            }
            Item {
                Layout.fillWidth: true
            }
            Button {
                text: "发送文件…"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                onClicked: fileDialog.open()
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

        // 拖拽入区（Doc18 §5 发送入口二）：整页接收文件拖放
        DropArea {
            Layout.fillWidth: true
            Layout.fillHeight: true
            keys: ["text/uri-list"]
            onDropped: drop => {
                if (drop.hasUrls) {
                    root.sendFiles(drop.urls);
                }
            }

            Rectangle {
                anchors.fill: parent
                radius: Theme.cardRadius
                border.color: parent.containsDrag ? Theme.primary : Theme.cardBorder
                border.width: 1
                color: "transparent"
            }

            ListView {
                id: listView
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                spacing: 10
                model: transfers

                // 空态：无传输任务时的占位提示（禁止假数据）
                Text {
                    anchors.centerIn: parent
                    visible: transfers.count === 0
                    text: "暂无传输任务\n连接设备后，点击\"发送文件\"或将文件拖入本区"
                    horizontalAlignment: Text.AlignHCenter
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    color: Theme.textHint
                }

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 64
                    radius: Theme.cardRadius
                    color: Theme.cardBg
                    border.color: Theme.cardBorder
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 14

                        Text {
                            text: model.dir === 0 ? "↑" : "↓"
                            font.pixelSize: 18
                            color: model.dir === 0 ? Theme.primary : Theme.accentBlue
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: model.name
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontBody
                                    color: Theme.textMain
                                    elide: Text.ElideRight
                                    Layout.maximumWidth: 320
                                }
                                Item {
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: model.done === 1 ? "已完成"
                                          : model.done === 2 ? "失败"
                                          : Math.round(model.progress * 100) + "%" + (model.speed ? " · " + model.speed : "")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontHint
                                    color: model.done === 1 ? Theme.accentGreen
                                           : model.done === 2 ? Theme.accentRed
                                           : Theme.textSecond
                                }
                            }
                            ProgressBar {
                                Layout.fillWidth: true
                                from: 0
                                to: 1
                                value: model.progress
                                background: Rectangle {
                                    implicitHeight: 6
                                    radius: 3
                                    color: Theme.divider
                                }
                                contentItem: Item {
                                    implicitHeight: 6
                                    Rectangle {
                                        width: parent.width * model.progress
                                        height: 6
                                        radius: 3
                                        color: model.done === 2 ? Theme.accentRed : Theme.primary
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
