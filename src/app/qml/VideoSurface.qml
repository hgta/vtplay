import QtQuick
import QtQuick.Controls.Basic
import VTPlay 1.0
import VTPlayCore 1.0

Item {
    id: root

    property bool hasMedia: false
    property string mediaUrl: ""

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

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onDoubleClicked: player.toggleFullscreen()
        onPressed: function(m) { drag.target = root; }
        onReleased: drag.target = null
        DropArea {
            anchors.fill: parent
            onDropped: function(drop) {
                if (drop.hasUrls && drop.urls.length > 0) {
                    player.openUrl(drop.urls[0]);
                }
            }
        }
    }

    // 拉鼠标移动激活控制条（简化：直接通过全局 hover）
    HoverHandler {
        id: hover
    }
}