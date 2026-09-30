import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import VTPlay 1.0

/// 侧栏：播放列表（本次会话队列）+ 最近打开（历史记录）。
/// 承载「切换视频不必重开文件对话框」这一核心诉求，未来也可放字幕轨等。
///
/// 注意：本组件不直接引用 Main.qml 里的 id（跨文件 id 不可见），
/// 需要提示/打开对话框时一律发信号由 Main 处理。
Rectangle {
    id: side
    implicitWidth: Theme.sidebarW
    color: Theme.panel

    signal openFileRequested()
    signal noticeRequested(string message)

    // ---------------------------------------------------------------- 组件

    /// 分组标题：标题 + 计数 + 清空
    component SectionHeader: Item {
        id: head
        property string title: ""
        property int    count: 0
        property bool   clearable: false

        signal cleared()

        implicitHeight: 30

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.padM
            anchors.rightMargin: Theme.padM
            spacing: Theme.padS

            Text {
                text: head.title
                color: Theme.textDim
                font.pixelSize: 11
                font.bold: true
            }
            Text {
                text: head.count > 0 ? String(head.count) : ""
                color: Theme.textDim
                font.pixelSize: 11
                opacity: 0.7
            }
            Item { Layout.fillWidth: true }

            Rectangle {
                visible: head.clearable
                Layout.preferredWidth: clearLabel.implicitWidth + Theme.padM * 2
                Layout.preferredHeight: 20
                radius: Theme.radiusS
                color: clearArea.containsMouse ? Theme.hover : "transparent"

                Text {
                    id: clearLabel
                    anchors.centerIn: parent
                    text: "清空"
                    color: Theme.textDim
                    font.pixelSize: 11
                }
                MouseArea {
                    id: clearArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: head.cleared()
                }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.border
        }
    }

    /// 列表行：文件名 + 当前项高亮 + 失效提示 + 可选移除
    component FileRow: Rectangle {
        id: row
        property string name: ""
        property bool   current: false
        property bool   missing: false
        property bool   removable: false
        property bool   clickable: true

        signal activated()
        signal removed()

        implicitHeight: Theme.rowH
        color: (rowArea.containsMouse && row.clickable) ? Theme.hover
             : row.current ? "#1F1F1F"
             : "transparent"

        // 当前项左侧强调条
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 3
            color: Theme.accent
            visible: row.current
        }

        Column {
            anchors.left: parent.left
            anchors.leftMargin: Theme.padM
            anchors.right: removeBtn.left
            anchors.rightMargin: Theme.padS
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1

            Text {
                width: parent.width
                text: row.name
                color: row.missing ? Theme.danger
                     : row.current ? Theme.accent
                     : Theme.text
                font.pixelSize: 12
                font.bold: row.current
                elide: Text.ElideMiddle
            }
            Text {
                width: parent.width
                visible: row.missing
                text: "文件已不存在"
                color: Theme.danger
                font.pixelSize: 10
                elide: Text.ElideRight
            }
        }

        // 悬停时出现「移除」（仅播放列表用）
        Text {
            id: removeBtn
            anchors.right: parent.right
            anchors.rightMargin: Theme.padM
            anchors.verticalCenter: parent.verticalCenter
            visible: row.removable && rowArea.containsMouse
            text: "✕"
            color: Theme.textDim
            font.pixelSize: 12
            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                cursorShape: Qt.PointingHandCursor
                onClicked: row.removed()
            }
        }

        MouseArea {
            id: rowArea
            anchors.fill: parent
            anchors.rightMargin: row.removable ? 24 : 0
            hoverEnabled: true
            enabled: row.clickable
            cursorShape: Qt.PointingHandCursor
            onClicked: row.activated()
        }
    }

    // ---------------------------------------------------------------- 布局

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ===== 播放列表 =====
        SectionHeader {
            Layout.fillWidth: true
            title: "播放列表"
            count: player.playlistCount
            clearable: player.playlistCount > 0
            onCleared: player.playlistClear()
        }

        // 空态：给出可操作的下一步，而不是一片空白
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 92
            visible: player.playlistCount === 0

            Column {
                anchors.centerIn: parent
                spacing: Theme.padS

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "队列为空"
                    color: Theme.textDim
                    font.pixelSize: 12
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "拖入多个文件即可排队"
                    color: Theme.textDim
                    font.pixelSize: 11
                    opacity: 0.7
                }
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: addLabel.implicitWidth + Theme.padL * 2
                    height: 26
                    radius: Theme.radiusS
                    color: addArea.containsMouse ? Theme.hover : "transparent"
                    border.width: 1
                    border.color: Theme.border

                    Text {
                        id: addLabel
                        anchors.centerIn: parent
                        text: "添加文件"
                        color: Theme.text
                        font.pixelSize: 11
                    }
                    MouseArea {
                        id: addArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: side.openFileRequested()
                    }
                }
            }
        }

        ListView {
            id: playlistView
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 280)
            Layout.topMargin: 4
            visible: player.playlistCount > 0
            clip: true
            model: player.playlist
            boundsBehavior: Flickable.StopAtBounds

            delegate: FileRow {
                required property var modelData
                width: playlistView.width
                name: modelData.name
                current: modelData.current
                missing: !modelData.exists
                removable: true
                onActivated: {
                    if (modelData.exists) player.playlistPlayAt(modelData.index)
                    else side.noticeRequested("文件已不存在：" + modelData.name)
                }
                onRemoved: player.playlistRemoveAt(modelData.index)
            }

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        }

        // ===== 循环模式 =====
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            Layout.topMargin: 4
            visible: player.playlistCount > 0

            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.padM
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                Repeater {
                    model: [
                        { mode: "none", label: "不循环" },
                        { mode: "one",  label: "单曲" },
                        { mode: "all",  label: "列表" }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool active: player.loopMode === modelData.mode
                        width: loopLabel.implicitWidth + Theme.padM * 2
                        height: 22
                        radius: Theme.radiusS
                        color: active ? Theme.accent
                             : loopArea.containsMouse ? Theme.hover
                             : "transparent"
                        border.width: active ? 0 : 1
                        border.color: Theme.border

                        Text {
                            id: loopLabel
                            anchors.centerIn: parent
                            text: modelData.label
                            color: parent.active ? Theme.bg : Theme.textDim
                            font.pixelSize: 11
                        }
                        MouseArea {
                            id: loopArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: player.loopMode = modelData.mode
                        }
                    }
                }
            }
        }

        Item { Layout.fillWidth: true; Layout.preferredHeight: Theme.padM }

        // ===== 最近打开 =====
        SectionHeader {
            Layout.fillWidth: true
            title: "最近打开"
            clearable: player.hasRecentFiles
            onCleared: player.clearRecentFiles()
        }

        Text {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            visible: !player.hasRecentFiles
            text: "暂无历史记录"
            color: Theme.textDim
            font.pixelSize: 11
            opacity: 0.7
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        ListView {
            id: recentView
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 200)
            Layout.topMargin: 4
            visible: player.hasRecentFiles
            clip: true
            model: player.recentFiles
            boundsBehavior: Flickable.StopAtBounds

            delegate: FileRow {
                required property var modelData
                width: recentView.width
                name: modelData.name
                missing: !modelData.exists
                clickable: modelData.exists
                onActivated: player.open(modelData.path)
            }

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        }

        Item { Layout.fillHeight: true }
    }

    // 右边界分隔线
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.border
    }
}
