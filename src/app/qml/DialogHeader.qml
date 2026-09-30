import QtQuick
import VTPlay 1.0

/// 对话框标题条。
///
/// 必须自己提供：Qt Quick Controls 的默认 header 用的是 Basic 浅色样式，
/// 在深色对话框顶部会留一条白带（实测在「设置」对话框上很明显）。
/// 背景由 Dialog 自己的 background 铺满整窗，这里只需摆文字。
Item {
    id: root

    property string text: ""

    implicitHeight: 46

    Text {
        anchors.left: parent.left
        anchors.leftMargin: Theme.padL
        anchors.right: parent.right
        anchors.rightMargin: Theme.padL
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        color: Theme.text
        font.pixelSize: 15
        font.bold: true
        elide: Text.ElideRight
    }
}
