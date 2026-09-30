import QtQuick
import QtQuick.Controls
import VTPlay 1.0

/// 倍速选择：控制条上显示当前倍速的紧凑按钮，点开是这个列表。
/// 替代浅色 ComboBox，保证控制条内所有控件同一套深色视觉。
Popup {
    id: menu
    width: 88
    padding: Theme.padS
    modal: false
    focus: true

    /// 当前倍速，由调用方绑定
    property real current: 1.0
    signal picked(real rate)

    readonly property var rates: [0.5, 0.75, 1.0, 1.25, 1.5, 2.0]

    background: Rectangle {
        color: Theme.panel
        radius: Theme.radiusS
        border.width: 1
        border.color: Theme.border
    }

    contentItem: Column {
        spacing: 1
        Repeater {
            model: menu.rates
            delegate: Rectangle {
                required property var modelData
                readonly property bool active: Math.abs(menu.current - modelData) < 0.001
                width: menu.width - Theme.padS * 2
                height: 24
                radius: Theme.radiusS
                color: actArea.containsMouse ? Theme.hover : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: (modelData === Math.round(modelData) ? modelData + ".0" : modelData) + "×"
                    color: parent.active ? Theme.accent : Theme.text
                    font.pixelSize: 12
                }
                MouseArea {
                    id: actArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        menu.picked(modelData)
                        menu.close()
                    }
                }
            }
        }
    }
}
