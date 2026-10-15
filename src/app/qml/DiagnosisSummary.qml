import QtQuick
import QtQuick.Layouts
import VTPlay 1.0

/// 源诊断摘要。
///
/// 设计取舍：这里**只呈现结论，不做决策**。
/// 诊断的价值在于让用户先知道"我这个源到底怎么了"，再自己决定要不要修复——
/// 而不是由程序替他判断、然后悄悄改动画质（对高质量源做修复就是破坏）。
///
/// 数据全部来自 `player.sourceDiagnosis`，且分级在 C++ 侧已经翻成
/// 「逐行」「轻度」这类短语，QML 不需要再做任何翻译或阈值判断。
ColumnLayout {
    id: root
    spacing: Theme.padS

    /// 诊断结论（QVariantMap）。字段见 PlayerController::sourceDiagnosis()。
    property var diagnosis: player.sourceDiagnosis
    /// 是否正在诊断
    readonly property bool running: player.diagnosisRunning
    /// 已拿到过有效结论
    readonly property bool hasResult: diagnosis && diagnosis.usable === true

    // 根节点是 ColumnLayout 而不是 Item：作为布局节点参与布局，父子宽度关系才是确定的。
    //
    // 注意：**不要**在这里写 `implicitWidth: 0`。ColumnLayout 的 implicitWidth 是布局
    // 自己算出来的，显式赋值会破坏它与父布局的宽度协商——实测后果是父布局按
    // "内容的自然宽度"给它分配（307px），而同级内联的 ColumnLayout 拿到的是
    // 容器全宽（528px）。想要"由容器决定宽度"，正确做法是在使用处写
    // `Layout.fillWidth: true`。

    // 注意：这里**不要**写 implicitWidth / Layout.preferredWidth。
    // 两者都试过，对本组件在父布局中的宽度没有任何影响（实测数值不变）。
    // 若要改变它的占宽行为，先看 ExportDialog.qml 里关于宽度的说明。

    ColumnLayout {
        id: col
        // 根节点已是布局，这里用 Layout.fillWidth 参与布局；
        // 布局的子项不能用 anchors（会与布局的宽度分配冲突）
        Layout.fillWidth: true
        spacing: Theme.padS

        // ---- 标题行 + 触发按钮 ----
        RowLayout {
            id: headerRow
            Layout.fillWidth: true
            spacing: Theme.padS

            Text {
                id: titleText
                text: "源诊断"
                color: Theme.text
                font.pixelSize: 13
                font.bold: true
            }
            Text {
                id: descText
                Layout.fillWidth: true
                text: root.hasResult ? "（这些结论决定是否需要修复）"
                                     : "（先看清问题，再决定要不要处理）"
                color: Theme.textDim
                font.pixelSize: 11
                elide: Text.ElideRight
            }
            FlatButton {
                id: btnStart
                text: root.running ? "诊断中…"
                                   : (root.hasResult ? "重新诊断" : "开始诊断")
                enabled: !root.running && player.hasMedia
                onClicked: player.runDiagnosis()
            }
            FlatButton {
                visible: root.running
                text: "取消"
                onClicked: player.cancelDiagnosis()
            }
        }

        // ---- 结论列表 ----
        // 用"标签 + 结论"两列而不是一段话：分级的可读性靠对齐，
        // 而不是靠用户从句子里找关键词。
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: root.hasResult

            Repeater {
                model: root.hasResult ? [
                    { k: "扫描方式", v: root.diagnosis.scan,
                      extra: root.diagnosis.scanForced ? "（手动指定）" : "" },
                    { k: "噪点/颗粒", v: root.diagnosis.detail, extra: "" },
                    { k: "画面抖动", v: root.diagnosis.shake, extra: "" },
                    { k: "画面边界", v: root.diagnosis.bars, extra: "" },
                    { k: "亮度", v: root.diagnosis.luma, extra: "" },
                    { k: "色彩", v: root.diagnosis.chroma, extra: "" }
                ] : []

                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Theme.padS

                    Text {
                        text: modelData.k
                        color: Theme.textDim
                        font.pixelSize: 12
                        Layout.preferredWidth: 72
                    }
                    Text {
                        Layout.fillWidth: true
                        // 「未知」要显式区分于「无」：把未知当正常会让用户
                        // 以为源没问题，而实际上只是没测出来
                        text: modelData.v + modelData.extra
                        color: modelData.v === "未知" ? "#E0A030" : Theme.text
                        font.pixelSize: 12
                    }
                }
            }
        }

        // ---- 未能诊断 / 提示 ----
        Text {
            Layout.fillWidth: true
            visible: !root.hasResult && !root.running
            text: player.hasMedia ? "尚未诊断。诊断需要数十秒，可随时取消。"
                                  : "打开视频后可进行源诊断。"
            color: Theme.textDim
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        // ---- 备注：哪些项降级成了未知、以及原因 ----
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            visible: root.hasResult && root.diagnosis.notes
                     && root.diagnosis.notes.length > 0

            Repeater {
                model: root.hasResult ? root.diagnosis.notes : []
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
    }
}
