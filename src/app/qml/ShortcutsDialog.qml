import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import VTPlay 1.0

/// 键盘快捷键一览（帮助菜单 / `?`）。
/// 快捷键定义集中在这里维护，与 Main.qml 的 Shortcut 列表一一对应。
Dialog {
    id: dlg
    title: "键盘快捷键"
    header: DialogHeader { text: dlg.title }
    modal: true
    anchors.centerIn: parent
    width: 460
    padding: Theme.padL

    // Basic 样式默认浅色底，而应用文字 token 是浅色的，必须覆盖为暗色面板
    background: Rectangle {
        color: Theme.panel
        radius: Theme.radiusM
        border.width: 1
        border.color: Theme.border
    }

    readonly property var rows: [
        { keys: "Space",        desc: "播放 / 暂停" },
        { keys: "← / →",        desc: "快退 / 快进 5 秒" },
        { keys: "Shift + ← / →", desc: "后退 / 前进 1 秒" },
        { keys: "Ctrl + ← / →",  desc: "逐帧后退 / 前进（播放中会自动暂停）" },
        { keys: "↑ / ↓",        desc: "音量增 / 减" },
        { keys: "M",            desc: "静音切换" },
        { keys: "F / 双击画面",  desc: "全屏切换" },
        { keys: "Ctrl + O",     desc: "打开文件" },
        { keys: "Ctrl + S",     desc: "截图" },
        { keys: "Ctrl + E",     desc: "导出 / 转换" },
        { keys: "Ctrl + L",     desc: "显示 / 隐藏侧栏" },
        { keys: "Ctrl + T",     desc: "始终置顶" },
        { keys: "Ctrl + PgUp/PgDn", desc: "上一项 / 下一项" },
        { keys: "F5",           desc: "设置" },
        { keys: "F10",          desc: "打开菜单（↑↓ 选择，→ 进子菜单，← 返回）" },
        { keys: "Ctrl + Q",     desc: "退出" },
        { keys: "?",            desc: "本窗口" }
    ]

    contentItem: ColumnLayout {
        spacing: Theme.padM

        Repeater {
            model: dlg.rows
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: Theme.padL

                Text {
                    Layout.preferredWidth: 150
                    text: modelData.keys
                    color: Theme.accent
                    font.family: "Consolas, Menlo, monospace"
                    font.pixelSize: 12
                }
                Text {
                    Layout.fillWidth: true
                    text: modelData.desc
                    color: Theme.text
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    footer: Item {
        implicitHeight: 44
        FlatButton {
            anchors.right: parent.right
            anchors.rightMargin: Theme.padL
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.padM
            text: "关闭"
            primary: true
            onClicked: dlg.close()
        }
    }
}
