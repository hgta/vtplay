import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import VTPlay 1.0

/// 设置：转码器（ffmpeg）与截图保存位置。
///
/// 只放「需要用户介入才能修好」的项——播放器本身不需要配置就能用，
/// 设置页若塞满可选项反而增加负担。转码器放在第一项，因为导出失败时
/// 用户是被红条提示「去设置」引到这里来的，必须一眼看到当前状态。
Dialog {
    id: dlg
    title: "设置"
    header: DialogHeader { text: dlg.title }
    modal: true
    anchors.centerIn: parent
    width: 560
    padding: Theme.padL

    background: Rectangle {
        color: Theme.panel
        radius: Theme.radiusM
        border.width: 1
        border.color: Theme.border
    }

    // ---------------------------------------------------------------- 小组件

    /// 分区标题
    component SectionTitle: Text {
        color: Theme.text
        font.pixelSize: 13
        font.bold: true
    }

    /// 等宽路径显示：可选中复制（路径常需要贴到别处）。
    /// 用 QtQuick 的 TextEdit 而非 Controls 的 TextArea——后者自带白底与内边距，
    /// 且 TextEdit 本身没有 background/padding 属性，写了会直接导致组件加载失败。
    component PathText: TextEdit {
        readOnly: true
        selectByMouse: true
        color: Theme.text
        font.pixelSize: 12
        font.family: "Consolas, Menlo, monospace"
        wrapMode: TextEdit.WrapAnywhere
        Layout.fillWidth: true
    }

    // ---------------------------------------------------------------- 内容

    contentItem: ColumnLayout {
        spacing: Theme.padM

        // ================= 转码器 =================
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.padS

            SectionTitle { text: "转码器" }

            Rectangle {
                width: 8; height: 8; radius: 4
                color: player.ffmpegAvailable ? Theme.accent : Theme.danger
            }
            Text {
                // 依赖 ffmpegAvailable/来源标签，探测结果一变就跟着变
                text: player.ffmpegAvailable
                      ? ("可用 · " + player.ffmpegSourceLabel)
                      : "未找到"
                color: player.ffmpegAvailable ? Theme.textDim : Theme.danger
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
        }

        Text {
            Layout.fillWidth: true
            color: Theme.textDim
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            text: "导出 / 转码需要外部的 ffmpeg。未指定时按「应用同级目录 → 系统 PATH」自动查找。"
        }

        PathText {
            text: player.ffmpegAvailable
                  ? player.ffmpegPath
                  : "（未找到可执行的 ffmpeg）"
            color: player.ffmpegAvailable ? Theme.text : Theme.textDim
            Layout.minimumHeight: 18
        }

        Text {
            Layout.fillWidth: true
            visible: player.ffmpegAvailable
            color: Theme.textDim
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
            maximumLineCount: 2
            // 先读 ffmpegPath 建立依赖：Q_INVOKABLE 不参与 QML 依赖追踪，
            // 只写 player.ffmpegVersionText() 的话换路径后不会重新求值。
            text: {
                var p = player.ffmpegPath
                return p.length > 0 ? player.ffmpegVersionText() : ""
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.padS

            FlatButton {
                text: "选择 ffmpeg…"
                onClicked: ffmpegDialog.open()
            }
            FlatButton {
                text: "自动探测"
                enabled: player.ffmpegConfiguredPath.length > 0
                onClicked: player.setFfmpegPath("")
            }
            Item { Layout.fillWidth: true }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // ================= 截图 =================
        SectionTitle { text: "截图保存位置" }

        PathText {
            text: player.screenshotDirectory
            Layout.minimumHeight: 18
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.padS

            FlatButton {
                text: "选择目录…"
                onClicked: shotDirDialog.open()
            }
            FlatButton {
                text: "恢复默认"
                enabled: player.screenshotDirectory !== player.defaultScreenshotDirectory()
                onClicked: player.setScreenshotDirectory("")
            }
            Item { Layout.fillWidth: true }
        }

        // 截图内容：默认「仅画面」，避免把控制条一起截进去
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.padS

            Text {
                Layout.preferredWidth: 72
                text: "截图内容"
                color: Theme.textDim
                font.pixelSize: 12
            }
            FlatButton {
                text: "仅画面"
                primary: player.screenshotContent === "frame"
                onClicked: player.setScreenshotContent("frame")
            }
            FlatButton {
                text: "含界面"
                primary: player.screenshotContent === "window"
                onClicked: player.setScreenshotContent("window")
            }
            Item { Layout.fillWidth: true }
        }

        Text {
            Layout.fillWidth: true
            color: Theme.textDim
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            text: "「仅画面」只保存视频帧本身（不含控制条）——需要连同进度条一起说明问题时选「含界面」。"
        }
    }

    footer: Item {
        implicitHeight: 44
        RowLayout {
            anchors.right: parent.right
            anchors.rightMargin: Theme.padL
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.padM
            spacing: Theme.padS

            FlatButton {
                text: "关闭"
                primary: true
                onClicked: dlg.close()
            }
        }
    }

    // ---------------------------------------------------------------- 文件选择

    FileDialog {
        id: ffmpegDialog
        title: "选择 ffmpeg 可执行文件"
        nameFilters: ["可执行文件 (*.exe)", "所有文件 (*)"]
        onAccepted: player.setFfmpegPath(player.localPathFromUrl(selectedFile))
    }

    FolderDialog {
        id: shotDirDialog
        title: "选择截图保存目录"
        onAccepted: player.setScreenshotDirectory(player.localPathFromUrl(selectedFolder))
    }
}
