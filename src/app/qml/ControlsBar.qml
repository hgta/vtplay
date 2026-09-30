import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VTPlay 1.0

Rectangle {
    id: bar
    height: 56

    /// 请求打开导出对话框（由 Main.qml 处理：工具栏不该自己持有弹窗）
    signal exportRequested()
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

        // 当前文件名：全屏时唯一能确认播放内容的位置。
        // 宽度受限 + 中间省略，保证长文件名不撑破控制条。
        Text {
            visible: player.fileName.length > 0
            text: player.fileName
            color: Theme.textDim
            font.pixelSize: 12
            elide: Text.ElideMiddle
            Layout.preferredWidth: Math.min(implicitWidth, 180)
            Layout.alignment: Qt.AlignVCenter
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

        // 倍速：紧凑按钮 + 深色弹出列表（替代 Basic 浅色 ComboBox）
        Rectangle {
            id: speedBtn
            Layout.preferredWidth: speedLabel.implicitWidth + Theme.padM * 2
            height: 28
            radius: Theme.radiusS
            color: speedArea.containsMouse ? Theme.hover : "transparent"
            border.width: 1
            border.color: Theme.border

            Text {
                id: speedLabel
                anchors.centerIn: parent
                text: {
                    var r = player.rate
                    return (r === Math.round(r) ? r.toFixed(1) : String(r)) + "×"
                }
                color: Theme.text
                font.pixelSize: 12
            }
            MouseArea {
                id: speedArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: speedMenu.opened ? speedMenu.close() : speedMenu.open()
            }

            SpeedMenu {
                id: speedMenu
                parent: speedBtn
                // 贴在按钮上方展开（控制条本来就位于窗口底部）
                x: 0
                y: -implicitHeight - 6
                current: player.rate
                onPicked: function(r) { player.rate = r }
            }
        }

        // 音量
        Row {
            spacing: 6
            height: 28

            Rectangle {
                width: 28; height: 28; radius: Theme.radiusS
                color: muteArea.containsMouse ? Theme.hover : "transparent"
                Text {
                    anchors.centerIn: parent
                    text: player.muted || player.volume === 0 ? "🔇" : "🔊"
                    color: Theme.text
                    font.pixelSize: 14
                }
                MouseArea {
                    id: muteArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: player.muted = !player.muted
                }
            }

            VolumeSlider {
                width: 100
                height: 28
                value: player.volume
                onMoved: function(v) { player.volume = v }
            }
        }

        // 导出（无媒体或转码器不可用时禁用）
        // 注意：自定义属性不能叫 enabled —— Item 基类已有该成员，会触发属性覆盖告警
        Rectangle {
            id: exportBtn
            width: 40; height: 40; radius: Theme.radiusS
            property bool actionEnabled: player.hasMedia && player.ffmpegAvailable
            opacity: actionEnabled ? 1.0 : 0.35
            color: (expArea.containsMouse && actionEnabled) ? Theme.border : "transparent"
            Text {
                anchors.centerIn: parent
                text: "⬇"
                color: Theme.text
                font.pixelSize: 16
            }
            MouseArea {
                id: expArea
                anchors.fill: parent
                hoverEnabled: true
                enabled: exportBtn.actionEnabled
                onClicked: bar.exportRequested()
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