import QtQuick

/// 扁平按钮：全应用统一的深色按钮外观。
/// 为什么不直接用 QtQuick.Controls 的 Button：Basic 样式是浅色主题，
/// 在深色界面上会出现「白底深字」的突兀块（导出对话框里曾实际出现）。
Rectangle {
    id: btn

    property string text: ""
    /// 主操作：实心荧光绿，一屏只放一个
    property bool primary: false
    /// 不叫 enabled：Item 基类已有该成员，覆盖会触发告警
    property bool actionEnabled: true
    property bool showBorder: !primary
    property int  hPadding: Theme.padL

    signal clicked()

    implicitWidth:  label.implicitWidth + hPadding * 2
    implicitHeight: 28
    radius: Theme.radiusS
    opacity: actionEnabled ? 1.0 : 0.4
    border.width: showBorder ? 1 : 0
    border.color: Theme.border

    color: {
        if (!actionEnabled) return "transparent"
        if (primary) return area.pressed ? Theme.accentDim : Theme.accent
        return area.pressed ? Theme.pressed
             : area.containsMouse ? Theme.hover
             : "transparent"
    }

    Behavior on color { ColorAnimation { duration: Theme.durFast } }

    Text {
        id: label
        anchors.centerIn: parent
        text: btn.text
        color: btn.primary ? Theme.bg : Theme.text
        font.pixelSize: 12
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        enabled: btn.actionEnabled
        cursorShape: Qt.PointingHandCursor
        onClicked: btn.clicked()
    }
}
