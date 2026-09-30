import QtQuick

/// 深色音量滑块。
/// 不用 QtQuick.Controls 的 Slider：Basic 样式是浅色主题，放在深色控制条上
/// 是一块白底，与整机视觉不搭（导出对话框曾因同样原因出现白底按钮）。
Item {
    id: root

    property real value: 1.0
    signal moved(real v)

    implicitHeight: 28

    function setFromX(px) {
        var v = px / Math.max(1, width)
        root.moved(Math.max(0.0, Math.min(1.0, v)))
    }

    // 轨道
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 4
        radius: 2
        color: Theme.border
    }

    // 已填充部分
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(0, parent.width * root.value)
        height: 4
        radius: 2
        color: Theme.accent
    }

    // 滑块
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        x: parent.width * root.value - width / 2
        width: 11
        height: 11
        radius: 6
        color: area.pressed || area.containsMouse ? Theme.accent : Theme.text
    }

    MouseArea {
        id: area
        anchors.fill: parent
        anchors.topMargin: -6
        anchors.bottomMargin: -6
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onPressed: function(m) { root.setFromX(m.x) }
        onPositionChanged: function(m) { if (pressed) root.setFromX(m.x) }
    }
}
