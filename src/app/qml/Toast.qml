import QtQuick

/// 轻量提示：截图保存、导出完成、条目失效等场景复用。
/// 不做模态、不抢焦点；带可选的操作按钮（如「打开所在文件夹」）。
Rectangle {
    id: toast

    property string message: ""
    property string actionText: ""
    /// 主动作回调；为空则只显示文字
    signal actionClicked()

    readonly property bool hasAction: actionText.length > 0

    width: Math.min(560, inner.implicitWidth + Theme.padL * 2)
    height: 40
    radius: Theme.radiusS
    color: Theme.panel
    border.width: 1
    border.color: Theme.border
    opacity: 0
    visible: opacity > 0.01

    /// 显示一条提示；durationMs 后自动淡出（传 0 表示不自动关闭）
    function show(msg, action, durationMs) {
        message = msg
        actionText = action === undefined ? "" : action
        hideTimer.interval = durationMs === undefined ? 3200 : durationMs
        if (hideTimer.interval > 0) hideTimer.restart()
        opacity = 1
    }

    function dismiss() {
        hideTimer.stop()
        opacity = 0
    }

    Behavior on opacity { NumberAnimation { duration: Theme.durNormal } }

    Timer { id: hideTimer; onTriggered: toast.dismiss() }

    Row {
        id: inner
        anchors.centerIn: parent
        spacing: Theme.padM

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: toast.message
            color: Theme.text
            font.pixelSize: 12
            elide: Text.ElideMiddle
            width: Math.min(implicitWidth, 360)
        }

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            visible: toast.hasAction
            width: actionLabel.implicitWidth + Theme.padM * 2
            height: 24
            radius: Theme.radiusS
            color: actionArea.containsMouse ? Theme.hover : "transparent"
            border.width: 1
            border.color: Theme.accent

            Text {
                id: actionLabel
                anchors.centerIn: parent
                text: toast.actionText
                color: Theme.accent
                font.pixelSize: 12
            }

            MouseArea {
                id: actionArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    toast.actionClicked()
                    toast.dismiss()
                }
            }
        }
    }

    MouseArea {
        // 点击提示本体即关闭（不遮挡其下方交互之外的内容）
        anchors.fill: parent
        z: -1
        onClicked: toast.dismiss()
    }
}
