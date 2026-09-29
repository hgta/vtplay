import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Dialogs
import VTPlay 1.0

ApplicationWindow {
    id: win
    width: 1280
    height: 720
    minimumWidth: 800
    minimumHeight: 500
    visible: true
    title: "VTPlay"
    color: Theme.bg

    // ===== 顶栏 =====
    header: Rectangle {
        height: 36
        color: Theme.bg
        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.padL
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Rectangle {
                width: 18; height: 18; radius: 6
                color: Theme.accent
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    anchors.centerIn: parent
                    text: "V"
                    color: Theme.bg
                    font.bold: true
                    font.pixelSize: 12
                }
            }
            Text {
                text: "VTPlay"
                color: Theme.text
                font.bold: true
                font.pixelSize: 13
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.padL
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Button {
                text: "打开"
                onClicked: fileDialog.open()
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: "选择媒体文件"
        nameFilters: ["视频 (*.mp4 *.mkv *.mov *.avi *.webm)", "音频 (*.mp3 *.flac *.wav *.aac *.ogg)", "所有文件 (*)"]
        onAccepted: player.openUrl(selectedFile)
    }

    // ===== 视频区 =====
    Item {
        anchors.fill: parent
        anchors.topMargin: 36

        // 1) 视频画面（最底层）
        VideoSurface {
            id: surface
            anchors.fill: parent
            hasMedia: player.hasVideo || player.hasAudio
            onPointerActivity: { controls.opacity = 1; hideTimer.restart() }
        }

        // 2) 错误提示
        Rectangle {
            z: 5
            visible: player.errorString.length > 0
            color: "#5A1F1F"
            border.color: "#FF6464"
            radius: Theme.radiusS
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 80
            anchors.horizontalCenter: parent.horizontalCenter
            width: errLabel.implicitWidth + 24
            height: errLabel.implicitHeight + 12
            Text {
                id: errLabel
                anchors.centerIn: parent
                text: player.errorString
                color: "#FFE0E0"
                font.pixelSize: 13
            }
        }

        // 3) 控制条：必须位于最上层（z 最高），否则会被视频区的鼠标区域遮挡而点不到。
        //    只通过 opacity 控制显隐（避免 visible/opacity 相互绑定的循环）。
        ControlsBar {
            id: controls
            z: 10
            opacity: 0
        }

        // 鼠标静止 3s 隐藏控制条；鼠标停留在控制条上时不隐藏（否则会打断拖动）
        Timer {
            id: hideTimer
            interval: 3000
            repeat: true
            running: true
            onTriggered: { if (!controls.hovered) controls.opacity = 0 }
        }
    }

    // ===== 快捷键 =====
    Shortcut { sequence: "Space";       onActivated: player.togglePlay() }
    Shortcut { sequence: "Ctrl+O";       onActivated: fileDialog.open() }
    Shortcut { sequence: "Right";        onActivated: player.seekDelta(5) }
    Shortcut { sequence: "Left";         onActivated: player.seekDelta(-5) }
    Shortcut { sequence: "Ctrl+Right";   onActivated: player.stepFrame() }
    Shortcut { sequence: "Ctrl+Left";    onActivated: player.seekDelta(-1) }
    Shortcut { sequence: "Up";           onActivated: player.volume = Math.min(1.0, player.volume + 0.05) }
    Shortcut { sequence: "Down";         onActivated: player.volume = Math.max(0.0, player.volume - 0.05) }
    Shortcut { sequence: "F";            onActivated: player.toggleFullscreen() }

    // 状态变化定期触发 QML 重读
    Timer {
        interval: 100
        running: true
        repeat: true
        onTriggered: {
            player.positionChanged()
            player.durationChanged()
            player.statusChanged()
        }
    }

    // 错误响应
    Connections {
        target: player
        function onFullscreenChanged() {
            if (player.fullscreen) win.showFullScreen();
            else                  win.showNormal();
        }
    }

    // QML 单例注册（qmldir）— CMake qt_add_qml_module 自动处理 URI
}