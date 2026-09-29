import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VTPlay 1.0

Rectangle {
    id: bar
    height: 56
    color: Theme.panel
    radius: Theme.radiusM
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.bottom: parent.bottom
    anchors.margins: 12
    // 显隐只由 opacity 单向驱动（visible 不可反向绑定 opacity，否则构成绑定循环）
    opacity: 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 200 } }

    /// 鼠标是否停留在控制条上（外层据此决定是否自动隐藏，避免打断拖动）。
    property alias hovered: barHover.containsMouse

    function fmtTime(s) { return Theme.fmtTime(s); }

    // 仅做悬停检测，不接收点击（acceptedButtons 为 NoButton 时不拦截子控件点击）
    MouseArea {
        id: barHover
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.padM
        spacing: Theme.padM

        // 播放/暂停
        Rectangle {
            width: 40; height: 40; radius: Theme.radiusS
            color: playArea.containsMouse ? Theme.accentDim : Theme.accent
            Text {
                anchors.centerIn: parent
                text: player.status === "Playing" ? "❚❚" : "▶"
                color: Theme.bg
                font.pixelSize: 16
                font.bold: true
            }
            MouseArea {
                id: playArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: player.togglePlay()
            }
        }

        // 时间码 / 总时长
        Text {
            text: fmtTime(player.position) + " / " + fmtTime(player.duration)
            color: Theme.text
            font.family: "Consolas, Menlo, monospace"
            font.pixelSize: 13
            Layout.preferredWidth: 130
        }

        // 进度条
        Item {
            Layout.fillWidth: true
            height: 28

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                height: 6
                radius: 3
                color: Theme.border
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                height: 6
                radius: 3
                color: Theme.accent
                width: parent.width * (player.duration > 0 ? player.position / player.duration : 0)
                Behavior on width { NumberAnimation { duration: 80 } }
            }

            MouseArea {
                anchors.fill: parent
                onPressed: function(m) {
                    var ratio = m.x / width;
                    if (player.duration > 0) player.seek(player.duration * ratio);
                }
                onPositionChanged: function(m) {
                    if (pressed) {
                        var ratio = m.x / width;
                        if (player.duration > 0) player.seek(player.duration * ratio);
                    }
                }
            }
        }

        // 倍速
        ComboBox {
            model: [0.5, 0.75, 1.0, 1.25, 1.5, 2.0]
            currentIndex: 2
            onActivated: player.rate = model[currentIndex]
            Layout.preferredWidth: 80
        }

        // 音量
        Row {
            spacing: 6
            Rectangle {
                width: 28; height: 28; radius: Theme.radiusS
                color: "transparent"
                Text {
                    anchors.centerIn: parent
                    text: player.muted || player.volume === 0 ? "🔇" : "🔊"
                    color: Theme.text
                    font.pixelSize: 14
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: player.muted = !player.muted
                }
            }
            Slider {
                from: 0; to: 1.0
                value: player.volume
                onMoved: player.volume = value
                Layout.preferredWidth: 100
            }
        }

        // 全屏
        Rectangle {
            width: 40; height: 40; radius: Theme.radiusS
            color: fsArea.containsMouse ? Theme.border : "transparent"
            Text {
                anchors.centerIn: parent
                text: player.fullscreen ? "🗗" : "🗖"
                color: Theme.text
                font.pixelSize: 16
            }
            MouseArea {
                id: fsArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: player.toggleFullscreen()
            }
        }
    }
}