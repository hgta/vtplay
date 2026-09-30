import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import VTPlay 1.0

/// 导出/转换对话框。
/// 四个阶段：idle（选预设）→ running（进度）→ done / failed。
/// 所有实战逻辑都在 C++ 的 Exporter 中，这里只负责呈现与命令转发。
Dialog {
    id: dlg
    title: "导出视频"
    header: DialogHeader { text: dlg.title }
    modal: true
    anchors.centerIn: parent
    width: 560
    padding: Theme.padL

    // QtQuick Controls Basic 默认是浅色背景，而本应用的文字 token 是浅色的
    // （Theme.text = #FFFFFF），若不覆盖底色会导致文字"消失"。这里统一为暗色面板。
    background: Rectangle {
        color: Theme.panel
        radius: Theme.radiusM
        border.color: Theme.border
        border.width: 1
    }

    /// idle | running | done | failed
    property string phase: "idle"
    property string presetId: "wechat-share"
    property string outputPath: ""
    property bool   userPickedPath: false
    property string resultPath: ""
    property string failMessage: ""
    property string failDetail: ""
    property bool   showDetail: false

    /// 用户在「未找到转码器」提示里点了「去设置…」：由 Main 打开设置页
    signal settingsRequested()

    // 注意：不要命名为 reset()——Popup 基类已有同名成员，会触发
    // "invalid override of property change signal or superclass signal"。
    function resetForm() {
        phase = "idle"
        presetId = "wechat-share"
        userPickedPath = false
        resultPath = ""
        failMessage = ""
        failDetail = ""
        showDetail = false
        outputPath = player.defaultOutputPath(presetId)
    }

    onOpened: resetForm()

    // 切换预设时刷新默认输出路径；用户手动选过则不覆盖
    onPresetIdChanged: {
        if (!userPickedPath) outputPath = player.defaultOutputPath(presetId)
    }

    Connections {
        target: player
        function onExportFinished(path) {
            dlg.phase = "done"
            dlg.resultPath = path
        }
        function onExportFailed(message, detail) {
            dlg.phase = "failed"
            dlg.failMessage = message
            dlg.failDetail = detail
        }
        function onExportCancelled() { dlg.phase = "idle" }
    }

    FileDialog {
        id: saveDialog
        title: "选择输出位置"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mp4"
        nameFilters: ["MP4 视频 (*.mp4)"]
        onAccepted: {
            dlg.outputPath = player.localPathFromUrl(selectedFile)
            dlg.userPickedPath = true
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.padM

        // ---- 源文件信息 ----
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text {
                text: player.fileName
                color: Theme.text
                font.pixelSize: 14
                font.bold: true
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            Text {
                text: player.specSummary
                color: Theme.textDim
                font.family: "Consolas, Menlo, monospace"
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        // ---- 转码器缺失提示 ----
        // 必须给出可执行的下一步：「请安装后重启」用户装完了也未必知道去哪指路径，
        // 这里直接把他送到设置页（导出失败时最需要的闭环）。
        Rectangle {
            Layout.fillWidth: true
            visible: !player.ffmpegAvailable
            color: "#5A1F1F"
            radius: Theme.radiusS
            implicitHeight: 34

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.padM
                anchors.rightMargin: Theme.padS
                spacing: Theme.padS

                Text {
                    Layout.fillWidth: true
                    text: "未找到转码器（ffmpeg），无法导出。"
                    color: "#FFE0E0"
                    font.pixelSize: 12
                }
                FlatButton {
                    text: "去设置…"
                    onClicked: {
                        dlg.close()
                        dlg.settingsRequested()
                    }
                }
            }
        }

        // ---- 预设列表 ----
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: dlg.phase === "idle" || dlg.phase === "failed"

            Repeater {
                model: player.exportPresets
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 48
                    radius: Theme.radiusS
                    color: dlg.presetId === modelData.id ? Theme.panel : "transparent"
                    border.width: 1
                    border.color: dlg.presetId === modelData.id ? Theme.accent : Theme.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.padM
                        anchors.rightMargin: Theme.padM
                        spacing: Theme.padM

                        Text {
                            text: dlg.presetId === modelData.id ? "●" : "○"
                            color: dlg.presetId === modelData.id ? Theme.accent : Theme.textDim
                            font.pixelSize: 14
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                text: modelData.label
                                color: Theme.text
                                font.pixelSize: 13
                            }
                            Text {
                                text: modelData.detail
                                color: Theme.textDim
                                font.pixelSize: 11
                            }
                        }
                        // 预估为**上界**（按目标码率算）；CRF 编码实际通常更小。
                        // estimate 由 C++ 随媒体信息一并下发（而不是在此调用
                        // Q_INVOKABLE）——Q_INVOKABLE 不参与依赖追踪，会导致
                        // 启动时求值一次后永不刷新。
                        Text {
                            // 自定义预设的体积随下方参数实时变化，列表内不重复展示
                            text: modelData.id === "custom" ? "自定义"
                                                            : ("≤ " + modelData.estimate)
                            color: Theme.textDim
                            font.family: "Consolas, Menlo, monospace"
                            font.pixelSize: 12
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: dlg.presetId = modelData.id
                    }
                }
            }
        }

        // ---- 自定义参数（仅选中"自定义"预设时显示）----
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.padS
            visible: dlg.presetId === "custom"
                     && (dlg.phase === "idle" || dlg.phase === "failed")

            Text { text: "短边"; color: Theme.textDim; font.pixelSize: 12 }
            // 属性不能叫 currentValue：Qt 6.9+ 的 ComboBox 已有同名 final 属性，覆盖会导致 QML 加载失败
            ComboBox {
                id: sideBox
                model: [480, 720, 1080, 1440, 2160]
                currentIndex: 1
                Layout.preferredWidth: 100
                readonly property int selectedValue: model[currentIndex]
            }
            Text { text: "码率"; color: Theme.textDim; font.pixelSize: 12 }
            ComboBox {
                id: kbpsBox
                model: [1000, 2000, 2500, 4000, 5000, 8000]
                currentIndex: 2
                Layout.preferredWidth: 110
                readonly property int selectedValue: model[currentIndex]
            }
            Item { Layout.fillWidth: true }
            Text {
                // 必须显式读取一个随媒体变化的属性来建立依赖：
                // Q_INVOKABLE 调用不参与 QML 依赖追踪，若只依赖 ComboBox 的值，
                // 本行会在启动（尚未加载媒体）时求值一次，之后永不刷新。
                text: {
                    const media = player.specSummary
                    return media.length >= 0
                        ? "≤ " + player.estimateCustom(sideBox.selectedValue,
                                                       kbpsBox.selectedValue)
                        : "≤ —"
                }
                color: Theme.textDim
                font.family: "Consolas, Menlo, monospace"
                font.pixelSize: 12
            }
        }

        // ---- 输出路径 ----
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.padS
            visible: dlg.phase === "idle" || dlg.phase === "failed"

            Text {
                text: "输出到"
                color: Theme.textDim
                font.pixelSize: 12
            }
            TextField {
                Layout.fillWidth: true
                text: dlg.outputPath
                font.pixelSize: 12
                selectByMouse: true
                // 不覆盖 color：Basic 样式的输入框是浅底，置为 Theme.text（白）会看不见
                onTextEdited: {
                    dlg.outputPath = text
                    dlg.userPickedPath = true
                }
            }
            Button {
                text: "浏览"
                onClicked: saveDialog.open()
            }
        }

        // ---- 导出中 ----
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.padS
            visible: dlg.phase === "running"

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "正在导出…"
                    color: Theme.text
                    font.pixelSize: 13
                    font.bold: true
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: (player.exportProgress >= 0
                           ? Math.round(player.exportProgress * 100) + "%"
                           : "—")
                          + "    速度 " + player.exportSpeed.toFixed(2) + "×"
                          + (player.exportWrittenText.length > 0
                             ? "    已写入 " + player.exportWrittenText : "")
                    color: Theme.textDim
                    font.family: "Consolas, Menlo, monospace"
                    font.pixelSize: 12
                }
            }
            Rectangle {
                Layout.fillWidth: true
                height: 6
                radius: 3
                color: Theme.border
                Rectangle {
                    height: parent.height
                    radius: 3
                    color: Theme.accent
                    width: parent.width * (player.exportProgress >= 0
                                           ? Math.min(1.0, player.exportProgress) : 0)
                }
            }
            Text {
                Layout.fillWidth: true
                text: dlg.outputPath
                color: Theme.textDim
                font.pixelSize: 11
                elide: Text.ElideMiddle
            }
        }

        // ---- 导出完成 ----
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.padS
            visible: dlg.phase === "done"

            Text {
                text: "✔ 导出完成"
                color: Theme.accent
                font.pixelSize: 14
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: dlg.resultPath
                color: Theme.text
                font.pixelSize: 12
                wrapMode: Text.WrapAnywhere
            }
        }

        // ---- 导出失败 ----
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.padS
            visible: dlg.phase === "failed"

            Text {
                Layout.fillWidth: true
                text: "✕ " + dlg.failMessage
                color: "#FF8080"
                font.pixelSize: 13
                font.bold: true
                wrapMode: Text.WordWrap
            }
            Text {
                visible: dlg.failDetail.length > 0
                text: dlg.showDetail ? "隐藏详情 ▴" : "查看详情 ▾"
                color: Theme.accent
                font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent
                    onClicked: dlg.showDetail = !dlg.showDetail
                }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                visible: dlg.showDetail
                Text {
                    text: dlg.failDetail
                    color: Theme.textDim
                    font.family: "Consolas, Menlo, monospace"
                    font.pixelSize: 11
                    wrapMode: Text.WrapAnywhere
                }
            }
        }
    }

    footer: RowLayout {
        spacing: Theme.padS

        Item { Layout.fillWidth: true }

        Button {
            text: "打开所在文件夹"
            visible: dlg.phase === "done"
            onClicked: player.openOutputFolder()
        }
        Button {
            text: "关闭"
            visible: dlg.phase === "done"
            onClicked: dlg.close()
        }
        Button {
            text: "取消导出"
            visible: dlg.phase === "running"
            onClicked: player.cancelExport()
        }
        Button {
            text: "取消"
            visible: dlg.phase === "idle" || dlg.phase === "failed"
            onClicked: dlg.close()
        }
        Button {
            text: "开始导出"
            highlighted: true
            visible: dlg.phase === "idle" || dlg.phase === "failed"
            enabled: player.ffmpegAvailable && dlg.outputPath.length > 0
            onClicked: {
                dlg.phase = "running"
                player.startExport(dlg.presetId, dlg.outputPath,
                                   sideBox.selectedValue, kbpsBox.selectedValue)
            }
        }
    }
}
