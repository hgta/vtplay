import QtQuick
import QtQuick.Controls.Basic
import VTPlay 1.0
import VTPlayCore 1.0

Item {
    id: root

    property bool hasMedia: false
    property string mediaUrl: ""

    /// 鼠标在视频区活动（移动/按下），供外层刷新控制条显隐计时。
    signal pointerActivity()

    VideoRenderer {
        id: renderer
        anchors.fill: parent
        visible: root.hasMedia
        Component.onCompleted: player.attachRenderer(renderer)
    }

    // 未打开媒体时的占位：VTPlay logo + 拖拽提示
    Column {
        anchors.centerIn: parent
        spacing: 16
        visible: !root.hasMedia

        Rectangle {
            width: 96; height: 96; radius: 22
            color: Theme.accent
            anchors.horizontalCenter: parent.horizontalCenter
            Text {
                anchors.centerIn: parent
                text: "V"
                font.pixelSize: 64
                font.bold: true
                color: Theme.bg
            }
        }
        Text {
            text: "VTPlay"
            color: Theme.text
            font.pixelSize: 28
            font.bold: true
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Text {
            text: "拖入文件，或点击“打开”"
            color: Theme.textDim
            font.pixelSize: 14
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    // 视频区鼠标交互：hover 上报（用于显示控制条）+ 双击全屏。
    // 注意不能吞掉点击事件——控制条位于更高 z 层，其上的点击由控制条自己处理。
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        onPositionChanged: root.pointerActivity()
        onPressed: root.pointerActivity()
        onDoubleClicked: player.toggleFullscreen()
    }

    // 拖拽文件到窗口打开
    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (drop.hasUrls && drop.urls.length > 0) {
                player.openUrl(drop.urls[0]);
            }
        }
    }
}