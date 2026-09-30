import QtQuick
import QtQuick.Layouts
import VTPlay 1.0

/// 顶栏：侧栏开关 + 品牌标识 + 当前文件名 + 常用操作。
/// 只放「高频、需要一眼看到」的操作（打开/截图/导出），其余进菜单，
/// 避免顶栏变成按钮仓库。
Rectangle {
    id: bar
    implicitHeight: Theme.topBarH
    color: Theme.bg

    signal menuRequested()
    signal openRequested()
    signal screenshotRequested()
    signal exportRequested()

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.padM
        anchors.rightMargin: Theme.padM
        spacing: Theme.padS

        // ---- 侧栏开关 ----
        FlatButton {
            text: "☰"
            hPadding: Theme.padM
            showBorder: false
            onClicked: bar.menuRequested()
        }

        // ---- 品牌标识 ----
        Row {
            Layout.leftMargin: Theme.padS
            spacing: 6
            Rectangle {
                width: 20; height: 20; radius: 6
                anchors.verticalCenter: parent.verticalCenter
                // 图标资源可用时用真标识，不可用时退回 "V" 方块（优雅降级）
                color: brandLogo.status === Image.Ready ? "transparent" : Theme.accent
                Image {
                    id: brandLogo
                    anchors.fill: parent
                    source: "qrc:/qt/qml/VTPlay/brand/logo.png"
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }
                Text {
                    anchors.centerIn: parent
                    visible: brandLogo.status !== Image.Ready
                    text: "V"
                    color: Theme.bg
                    font.bold: true
                    font.pixelSize: 12
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "VTPlay"
                color: Theme.text
                font.bold: true
                font.pixelSize: 13
            }
        }

        // ---- 当前文件名（居中；未打开媒体时给一句上手提示）----
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.padL
            Layout.rightMargin: Theme.padL
            text: player.hasMedia ? player.fileName
                                  : "把文件拖到这里，或按 Ctrl+O 打开"
            color: player.hasMedia ? Theme.text : Theme.textDim
            font.pixelSize: 13
            font.bold: player.hasMedia
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
            // 全屏时顶栏隐藏，此提示不会出现在画面上
        }

        // ---- 常用操作 ----
        FlatButton {
            text: "截图"
            actionEnabled: player.hasMedia
            onClicked: bar.screenshotRequested()
        }
        FlatButton {
            text: "导出"
            actionEnabled: player.hasMedia && player.ffmpegAvailable
            onClicked: bar.exportRequested()
        }
        FlatButton {
            text: "打开"
            primary: true
            onClicked: bar.openRequested()
        }
    }
}
