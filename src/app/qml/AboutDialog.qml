import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import VTPlay 1.0

/// 关于：版本、构建、运行环境与许可说明。
/// 「复制信息」便于用户提交问题报告时附带环境。
Dialog {
    id: dlg
    title: "关于 VTPlay"
    // 内容首行已经是品牌区（图标 + VTPlay + 版本），再叠一条标题就是重复
    header: null
    modal: true
    anchors.centerIn: parent
    width: 520
    padding: Theme.padL

    background: Rectangle {
        color: Theme.panel
        radius: Theme.radiusM
        border.width: 1
        border.color: Theme.border
    }

    readonly property var rows: [
        { k: "版本",      v: player.appVersion },
        { k: "构建时间",  v: player.buildTimestamp },
        { k: "Qt",        v: player.qtVersion },
        { k: "转码器",    v: player.ffmpegAvailable
                               ? (player.ffmpegPath + "\n" + player.ffmpegVersionText())
                               : "未找到（导出功能不可用）" },
        { k: "音频输出",  v: player.audioDeviceName() }
    ]

    function plainText() {
        var lines = ["VTPlay " + player.appVersion, "构建时间: " + player.buildTimestamp,
                     "Qt: " + player.qtVersion]
        for (var i = 0; i < rows.length; ++i) {
            lines.push(rows[i].k + ": " + String(rows[i].v).replace(/\n/g, " | "))
        }
        return lines.join("\n")
    }

    contentItem: ColumnLayout {
        spacing: Theme.padM

        // ---- 品牌区 ----
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.padM

            Rectangle {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 44
                radius: 12
                color: aboutLogo.status === Image.Ready ? "transparent" : Theme.accent

                Image {
                    id: aboutLogo
                    anchors.fill: parent
                    source: "qrc:/qt/qml/VTPlay/brand/logo.png"
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }
                Text {
                    anchors.centerIn: parent
                    visible: aboutLogo.status !== Image.Ready
                    text: "V"
                    color: Theme.bg
                    font.bold: true
                    font.pixelSize: 22
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: "VTPlay"
                    color: Theme.text
                    font.pixelSize: 18
                    font.bold: true
                }
                Text {
                    text: "轻量本地视频播放器 · " + player.appVersion
                    color: Theme.textDim
                    font.pixelSize: 12
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // ---- 环境信息 ----
        Repeater {
            model: dlg.rows
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: Theme.padL

                Text {
                    Layout.preferredWidth: 72
                    Layout.alignment: Qt.AlignTop
                    text: modelData.k
                    color: Theme.textDim
                    font.pixelSize: 12
                }
                Text {
                    Layout.fillWidth: true
                    text: modelData.v
                    color: Theme.text
                    font.pixelSize: 12
                    wrapMode: Text.WrapAnywhere
                    font.family: "Consolas, Menlo, monospace"
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // ---- 许可 ----
        Text {
            Layout.fillWidth: true
            color: Theme.textDim
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            lineHeight: 1.25
            text: "本应用以 Qt（LGPLv3）构建，调用外部 ffmpeg 进程完成导出。\n"
                + "当前分发的 ffmpeg 为 GPL 构建（含 x264/x265），其许可与源码"
                + "获取方式随附于发布包；导出以独立进程调用，不构成衍生作品。"
        }

        // 隐藏的 TextEdit 仅用于复制到剪贴板
        TextEdit { id: clip; visible: false }
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
                text: "复制信息"
                onClicked: {
                    clip.text = dlg.plainText()
                    clip.selectAll()
                    clip.copy()
                }
            }
            FlatButton {
                text: "关闭"
                primary: true
                onClicked: dlg.close()
            }
        }
    }
}
