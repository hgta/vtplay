import QtQuick
import QtQuick.Layouts
import VTPlay 1.0

/// 媒体信息条：当前媒体的技术规格（分辨率/帧率/码率/体积 + 音频参数）。
///
/// 与顶栏的分工：文件名在顶栏居中大字展示（一眼可见），这里放「想确认细节时才看」
/// 的技术参数。两者若都显示文件名，只会形成相邻两行的重复。
/// 未加载媒体时整体隐藏（不占位、不残留上一个文件的信息）。
Rectangle {
    id: bar
    height: Theme.infoBarH
    color: Theme.bg
    visible: player.hasMedia

    // 底部分隔线：弱化视觉权重，不抢画面注意力
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.padL
        anchors.rightMargin: Theme.padL
        spacing: Theme.padM

        // 规格 + 音频：缺失字段由核心层自动省略，两段都为空时本行自然留白
        Text {
            text: {
                var parts = []
                if (player.specSummary.length > 0)  parts.push(player.specSummary)
                if (player.audioSummary.length > 0) parts.push(player.audioSummary)
                return parts.join("  ·  ")
            }
            color: Theme.textDim
            font.family: "Consolas, Menlo, monospace"
            font.pixelSize: 11
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
        }

        // 转码器不可用时前置说明导出为何置灰（避免用户以为按钮坏了）
        Text {
            visible: !player.ffmpegAvailable
            text: "未找到 ffmpeg，导出不可用"
            color: Theme.danger
            font.pixelSize: 11
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
