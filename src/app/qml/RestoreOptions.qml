import QtQuick 2.15
import QtQuick.Layouts 1.15
import VTPlay 1.0

/// 修复选项：把「用户意图」翻译成少量语义开关。
///
/// 设计取舍：
///  · 组件**不做任何推导**。该做什么由后端 `buildPlan`（纯函数、可穷举断言）决定，
///    界面只负责读写意图 + 呈现结论。于是"决策"全仓库只有一处，
///    界面不可能与后端算出的方案不一致——这是刻意的，不是偷懒。
///  · 只暴露语义开关（强度/保颗粒/尺寸/编码取向），不暴露滤镜参数。
///    用户想表达的是"让它清楚点"，不是"hqdn3d 的 luma_spatial 设多少"。
///  · 意图走整份 map 读写（`player.restoreIntent`），两端不会各存一份而漂移。
ColumnLayout {
    id: root
    spacing: Theme.padS
    // 不设 implicitWidth：ColumnLayout 的 implicitWidth 由它自己算，
    // 显式赋值会破坏与父布局的宽度协商（详见 DiagnosisSummary.qml 的说明）。

    /// 输出路径由容器传入（对话框持有它，组件不该自己造一个默认路径，
    /// 否则用户改了路径、这里却导到别处去了）。
    property string outputPath: ""

    readonly property var  intent:  player.restoreIntent
    readonly property var  preview: player.restorePreview
    /// 已拿到可用诊断结论（未诊断时不能推导方案）
    readonly property bool ready:   preview && preview.usable === true
    /// 方案可用（诊断有结论、顺序约束成立）
    readonly property bool canRun:  ready && preview.valid === true
                              && outputPath.length > 0 && !player.exportRunning

    /// 方案里是否含稳定阶段。预览会跳过它（单帧无法执行两遍处理），
    /// 所以必须据此如实标注——否则用户会以为稳定没生效。
    readonly property bool hasStabilize: {
        if (!ready) return false
        var st = preview.stages || []
        for (var i = 0; i < st.length; ++i) if (st[i].kind === 2) return true
        return false
    }

    /// 改一个字段：整份 map 回写。后端按值比较，无变化不会重算。
    function patch(key, value) {
        var m = {}
        for (var k in intent) m[k] = intent[k]
        m[key] = value
        player.restoreIntent = m
    }

    // ---- 标题 ----
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.padS
        Text {
            text: "修复设置"
            color: Theme.text
            font.pixelSize: 13
            font.bold: true
        }
        Text {
            Layout.fillWidth: true
            text: "（强度越高越干净，但越可能连细节一起磨掉）"
            color: Theme.textDim
            font.pixelSize: 11
            elide: Text.ElideRight
        }
        Item { Layout.fillWidth: true }
    }

    // ---- 处理强度 ----
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.padS
        Text {
            text: "处理强度"
            color: Theme.textDim
            font.pixelSize: 12
        }
        Repeater {
            model: [
                { v: 0, t: "不处理" },
                { v: 1, t: "轻度" },
                { v: 2, t: "中度" },
                { v: 3, t: "强力" }
            ]
            delegate: FlatButton {
                required property var modelData
                text: modelData.t
                primary: root.intent.strength === modelData.v
                onClicked: root.patch("strength", modelData.v)
            }
        }
        Item { Layout.fillWidth: true }
    }

    // ---- 保颗粒 ----
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.padS
        Text {
            text: "颗粒"
            color: Theme.textDim
            font.pixelSize: 12
        }
        FlatButton {
            text: root.intent.keepGrain ? "保留（推荐）" : "消除"
            primary: root.intent.keepGrain === true
            onClicked: root.patch("keepGrain", !root.intent.keepGrain)
        }
        Text {
            Layout.fillWidth: true
            text: "老片的颗粒是质感，不是噪声；消除会让画面变干净也变塑料。"
            color: Theme.textDim
            font.pixelSize: 11
            elide: Text.ElideRight
        }
        Item { Layout.fillWidth: true }
    }

    // ---- 输出尺寸 ----
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.padS
        Text {
            text: "输出尺寸"
            color: Theme.textDim
            font.pixelSize: 12
        }
        Repeater {
            model: [
                { v: 0,    t: "保持原样" },
                { v: 720,  t: "720" },
                { v: 1080, t: "1080" },
                { v: 1440, t: "1440" },
                { v: 2160, t: "2160" }
            ]
            delegate: FlatButton {
                required property var modelData
                text: modelData.t
                primary: root.intent.targetShortSide === modelData.v
                onClicked: root.patch("targetShortSide", modelData.v)
            }
        }
        Item { Layout.fillWidth: true }
    }
    // 这一条必须说在前面：把糊的 480p 拉到 4K 只会得到一个"很大的糊文件"。
    // 用户最容易误解的就是这里，与其让他导出完再失望，不如现在讲清楚。
    Text {
        Layout.fillWidth: true
        visible: root.intent.targetShortSide > 0
        text: "放大只是插值，不会凭空生出细节；分辨率变大不等于更清晰。"
        color: Theme.textDim
        font.pixelSize: 11
        wrapMode: Text.WordWrap
    }

    // ---- 编码取向 ----
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.padS
        Text {
            text: "编码"
            color: Theme.textDim
            font.pixelSize: 12
        }
        FlatButton {
            text: "质量优先 HEVC 10bit"
            // 强度为「不处理」时后端固定按兼容档输出（见 RestorePlan.cpp 的 tenBit 表达式），
            // 此时这个选择不生效。与其让用户以为选了 10bit 却拿到 8bit，
            // 不如把按钮禁用并说明原因——控件不能承诺它做不到的事。
            enabled: root.intent.strength !== 0
            primary: root.intent.strength !== 0 && root.intent.preferCompatibility !== true
            onClicked: root.patch("preferCompatibility", false)
        }
        FlatButton {
            text: "兼容优先 H.264 8bit"
            enabled: root.intent.strength !== 0
            primary: root.intent.strength === 0 || root.intent.preferCompatibility === true
            onClicked: root.patch("preferCompatibility", true)
        }
        Item { Layout.fillWidth: true }
    }

    // 「不处理」时编码取向不生效，必须说清楚：
    // 这是**设计**（不修复即走兼容档），但界面不能因此默默忽略用户的选择。
    Text {
        Layout.fillWidth: true
        visible: root.intent.strength === 0
        text: "强度为「不处理」时按兼容档输出（H.264 8bit），编码取向不生效。"
        color: Theme.textDim
        font.pixelSize: 11
        wrapMode: Text.WordWrap
    }

    // ---- 色彩校正 ----
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.padS
        Text {
            text: "亮度/色彩校正"
            color: Theme.textDim
            font.pixelSize: 12
        }
        FlatButton {
            text: root.intent.colorFix ? "开" : "关"
            primary: root.intent.colorFix === true
            onClicked: root.patch("colorFix", !root.intent.colorFix)
        }
        Item { Layout.fillWidth: true }
    }

    // ---- 将要执行什么（提示，而非代替用户决定）----
    Text {
        Layout.fillWidth: true
        visible: !root.ready
        text: "先点上方「开始诊断」——诊断结论决定处理哪些问题，没有结论就无从推导方案。"
        color: Theme.textDim
        font.pixelSize: 11
        wrapMode: Text.WordWrap
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 3
        visible: root.ready

        Text {
            Layout.fillWidth: true
            text: (root.preview.onlyEncode ? "不需要修复，仅重新编码：" : "将执行：")
                  + root.preview.summary
            color: Theme.text
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            visible: root.preview.valid === false
            text: "方案不合法，已禁止导出：" + root.preview.invalidReason
            color: Theme.danger
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        // 帧数减少是**正确结果**（反胶转），必须在导出前说明，
        // 否则用户看到"30fps 进去、24fps 出来"会认定导出坏了。
        Text {
            Layout.fillWidth: true
            visible: root.preview.changesFrameCount === true
            text: "反胶转会减少帧数（如 30 → 24fps），这是正确结果，不是丢帧。"
            color: Theme.textDim
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        Repeater {
            model: root.ready && root.preview.notes ? root.preview.notes : []
            delegate: Text {
                required property string modelData
                Layout.fillWidth: true
                text: "· " + modelData
                color: Theme.textDim
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
    }

    // ---- 预览一帧 ----
    //
    // "清晰"没有客观标准，但"我喜不喜欢"用户看一眼就知道——所以这是唯一能
    // 可靠管理预期的办法，尤其是在花几十分钟导出之前。
    // 渲染是同步的（实测 < 1 秒），点完直接出图。
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.padS
        FlatButton {
            text: "预览一帧（当前位置）"
            enabled: root.canRun
            onClicked: player.renderRestorePreview(player.position)
        }
        Text {
            Layout.fillWidth: true
            text: player.restorePreviewError.length > 0
                  ? "预览失败：" + player.restorePreviewError
                  : (root.hasStabilize
                     ? "预览不含稳定效果（稳定需两遍处理，单帧无法执行）"
                     : "预览用的是将要执行的同一套滤镜链")
            color: player.restorePreviewError.length > 0 ? Theme.danger : Theme.textDim
            font.pixelSize: 11
            elide: Text.ElideRight
        }
        Item { Layout.fillWidth: true }
    }

    // ---- 启动修复导出 ----
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.padS
        FlatButton {
            text: player.exportRunning ? "导出中…" : "按此方案导出"
            primary: true
            enabled: root.canRun
            onClicked: player.startRestoreExport(root.outputPath)
        }
        Text {
            Layout.fillWidth: true
            text: player.exportRunning && player.exportStageText
                  ? "阶段（估算）：" + player.exportStageText
                  : (root.ready && root.preview.valid === false
                     ? "方案不合法，无法导出"
                     : "")
            color: Theme.textDim
            font.pixelSize: 11
            elide: Text.ElideRight
        }
        Item { Layout.fillWidth: true }
    }

    // ---- 预览图 ----
    //
    // **必须放在导出按钮之后**：实测把它放在按钮之前时，出图会把按钮挤到
    // 可视区之外，而对话框不可滚动——按钮就彻底按不到了，功能等于坏掉。
    // 图是可选的辅助信息，按钮是必须能按到的操作，顺序不能反。
    // 高度也压到 120：它是"看一眼"用的，不需要占满半个屏幕。
    Image {
        Layout.fillWidth: true
        visible: player.restorePreviewImage.length > 0
        source: player.restorePreviewImage
        fillMode: Image.PreserveAspectFit
        Layout.preferredHeight: 120
        Layout.maximumHeight: 120
        // 关掉缓存：预览图路径固定、靠 URL 序号区分，
        // 开着缓存会命中旧图（表现为"改了设置但预览没变"）。
        cache: false
    }
}
