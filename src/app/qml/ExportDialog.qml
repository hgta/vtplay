import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
// 见 Main.qml 的说明：Qt5 的 Dialogs 1.3 自带 `Dialog`，会遮蔽 Controls 的 `Dialog`，
// 导致本文件的根 Dialog 失去 footer 等属性。用别名导入避免歧义。
import QtQuick.Dialogs 1.3 as NativeDialogs
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
        // 重置时导出可能**已经在跑**（修复导出的按钮就在本对话框里），不能无脑置回 idle：
        // 置回去界面上立刻变成"没在导出"——没有进度、没有阶段行、没有取消按钮，
        // 而且「开始导出」按钮会重新出现，用户能叠一个第二路导出上去。
        //
        // 这里必须主动判断，不能只靠下面的 exportStateChanged：
        // 它只在起/停时发一次，进度刷新不会重发；对话框重开时那一次早就错过了。
        phase = player.exportRunning ? "running" : "idle"
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

        // 导出中状态以 exportRunning 为准，而不是"点了按钮就置 running"。
        //
        // 原因：置 running 的地方只有点击导出按钮那一处，而那属于**分享导出**路径。
        // 修复导出走的是另一个入口（按钮在 RestoreOptions 里），不在这里接上的话，
        // 它会全程停在 idle 阶段——没有进度、没有阶段名、**也没有取消按钮**，
        // 一个要跑几十分钟的任务就这样变成一个无法中止的黑盒。
        function onExportStateChanged() {
            if (player.exportRunning && dlg.phase !== "running") dlg.phase = "running"
        }
    }

    NativeDialogs.FileDialog {
        id: saveDialog
        title: "选择输出位置"
        // Qt5 的 QtQuick.Dialogs 1.3 没有 fileMode，"另存为"用 selectExisting: false 表达
        selectExisting: false
        defaultSuffix: "mp4"
        nameFilters: ["MP4 视频 (*.mp4)"]
        onAccepted: {
            dlg.outputPath = player.localPathFromUrl(fileUrl)
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

        // ---- 源诊断 ----
        // 放在**预设列表之前**：诊断的用途是帮用户决定"要不要处理、怎么处理"，
        // 而决策点就是下面的预设选择。放在决策之后等于白说。
        // 未诊断时它只占一行（"源诊断 + 开始诊断"），不会挤压预设列表。
        // 组件只呈现结论、不做决策（见 DiagnosisSummary.qml 的说明）。
        // 宽度说明：这两个组件**不会铺满容器宽度**，只占自身内容的自然宽度
        // （实测 307 / 454，而容器是 528）。功能不受影响——组件内部各行都是
        // "容器宽度 = 自身内容宽度"，所以按钮完整可见、没有被裁。
        // 至于为什么同为 ColumnLayout 根节点，内联的能拿到 528、组件实例拿不到，
        // 尚未查明（已排除 implicitWidth: 0 与 Layout.preferredWidth: 0）。
        // 这里**不做未经验证的修补**：宁可留一个已实测的外观缺陷，
        // 也不留一个"看起来更对但没测过"的改法。详见 tasks.md 5b.4。
        DiagnosisSummary {
            Layout.fillWidth: true
            visible: player.hasMedia
        }

        // ---- 修复设置 ----
        // 紧跟诊断之后：先看"源怎么了"，再决定"怎么处理"，顺序即用户的思考顺序。
        // outputPath 由对话框传入——组件不该自己造默认路径，否则用户改了路径、
        // 这里却导到别处去了。
        RestoreOptions {
            Layout.fillWidth: true
            visible: player.hasMedia
            outputPath: dlg.outputPath
        }

        // 【为什么两个组件要包一层 Item，而不是直接用 Layout.fillWidth】
        //
        // 实测：在同一个 contentItem 里，**内联**的 ColumnLayout 能拿到容器全宽 528，
        // 而自定义组件（根节点同为 ColumnLayout）只拿到自身内容的自然宽度
        // （DiagnosisSummary 307、RestoreOptions 454）。给了 Layout.fillWidth: true
        // 也一样，且排除过 implicitWidth: 0 / Layout.preferredWidth: 0 的影响
        // （删掉这两项后数值完全没变）。**成因未查明**，因此不继续押假设，
        // 改用"外层 Item 承接布局宽度、组件按 parent.width 跟随"这一确定有效的做法。
        //
        // 判定依据是宽度而不是截图——截图会因 dpr 换算出错，见 tasks.md 5b.4。

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

            // 显示**完整方案**，而不是"当前阶段"。
            //
            // 原因是一个原理性限制：所有修复阶段串在**同一条 ffmpeg 滤镜链、一次主遍**
            // 里完成（刻意的效率选择——每阶段起一个进程要反复落无损中间文件，
            // 磁盘与耗时都不可接受）。因此主遍里**根本不存在阶段边界**，
            // 精确的"当前阶段"拿不到，只能由总进度近似推断。
            // 声称一个近似的阶段名是在编造精度；不如告诉用户"它到底要做什么"——
            // 这才是他真正需要的信息，也是他能在几十分钟等待中判断是否合理的依据。
            Text {
                Layout.fillWidth: true
                visible: player.restorePreview.usable === true
                text: (player.restorePreview.onlyEncode ? "仅重新编码：" : "将执行：")
                      + player.restorePreview.summary
                color: Theme.accent
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            // 阶段名只作**粗略提示**，故明确标注为估算。
            // 修复各阶段耗时差异可达十倍以上（降噪/稳定远慢于锐化），
            // 只有总百分比时用户无法区分"卡住了"和"正在跑慢阶段"。
            Text {
                Layout.fillWidth: true
                visible: player.exportStageText.length > 0
                text: "阶段（估算）：" + player.exportStageText
                color: Theme.textDim
                font.pixelSize: 11
                elide: Text.ElideRight
            }

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
