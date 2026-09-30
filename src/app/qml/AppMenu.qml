import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import VTPlay 1.0

/// 主菜单（两级）：顶级只有「文件 / 播放 / 视图 / 帮助」，各项展开为子菜单。
///
/// 为什么不做成一整列：全部命令平铺有 20 多条，逻辑可用高度只有 672（150% 缩放的小屏）
/// 时，底部条目必然被裁掉或需要滚动——用户点不到「视图」里的功能。
/// 两级菜单是桌面软件的通行做法，顶级恒定 4 项，任一屏都能完整显示。
///
/// 键盘（F10 打开，见 Main.qml）：
///   ↑/↓     在当前层级上下移动（顶级移动时右侧子菜单跟随切换）
///   →/Enter 进入子菜单
///   ←       退回顶级（已在顶级则关闭）
///   Enter   在子菜单中执行该项
///   Esc     直接关闭整个菜单
/// 鼠标与键盘共用同一份高亮状态：悬停会同步 index/subIndex，两种输入不会各亮各的。
///
/// 菜单项只声明「显示什么 + 发哪个 action」，真正的命令接线集中在 Main.qml，
/// 避免菜单文本与快捷键定义散落两处。
Popup {
    id: menu
    width: 132
    padding: Theme.padS
    modal: false
    focus: true

    /// 点击叶子项后抛出动作 id，由 Main.qml 统一处理
    signal triggered(string action)

    /// 勾选态由 Main.qml 提供：把「哪个状态」翻译成 bool，菜单本身不持有状态
    property var checkedState: function(what) { return false }

    readonly property var groups: [
        {
            title: "文件",
            items: [
                { action: "open",       label: "打开文件…",       shortcut: "Ctrl+O" },
                { action: "addFiles",   label: "添加到播放列表…", shortcut: "" },
                { action: "revealFile", label: "在文件夹中显示",  shortcut: "" },
                { separator: true },
                { action: "screenshot", label: "截图…",           shortcut: "Ctrl+S" },
                { action: "export",     label: "导出 / 转换…",     shortcut: "Ctrl+E" },
                { separator: true },
                { action: "settings",   label: "设置…",           shortcut: "F5" },
                { separator: true },
                { action: "quit",       label: "退出",            shortcut: "Ctrl+Q" }
            ]
        },
        {
            title: "播放",
            items: [
                { action: "togglePlay", label: "播放 / 暂停", shortcut: "Space" },
                { action: "prev",       label: "上一项",      shortcut: "Ctrl+PgUp" },
                { action: "next",       label: "下一项",      shortcut: "Ctrl+PgDn" },
                { separator: true },
                // 精度递进：无修饰 = 5 秒，Shift = 1 秒，Ctrl = 单帧
                { action: "back5",      label: "快退 5 秒",   shortcut: "←" },
                { action: "fwd5",       label: "快进 5 秒",   shortcut: "→" },
                { action: "back1",      label: "后退 1 秒",   shortcut: "Shift+←" },
                { action: "fwd1",       label: "前进 1 秒",   shortcut: "Shift+→" },
                { action: "stepBack",   label: "逐帧后退",    shortcut: "Ctrl+←" },
                { action: "stepFrame",  label: "逐帧前进",    shortcut: "Ctrl+→" },
                { separator: true },
                { action: "mute",       label: "静音切换",    shortcut: "M" },
                { action: "cycleLoop",  label: "循环模式",    shortcut: "" }
            ]
        },
        {
            title: "视图",
            items: [
                { action: "toggleSidebar", label: "侧栏",     shortcut: "Ctrl+L",
                  checkable: true, checkedWhen: "sidebar" },
                { action: "fullscreen",    label: "全屏",     shortcut: "F",
                  checkable: true, checkedWhen: "fullscreen" },
                { action: "alwaysOnTop",   label: "始终置顶", shortcut: "Ctrl+T",
                  checkable: true, checkedWhen: "alwaysOnTop" }
            ]
        },
        {
            title: "帮助",
            items: [
                { action: "shortcuts", label: "键盘快捷键…", shortcut: "?" },
                { action: "about",     label: "关于 VTPlay",  shortcut: "" }
            ]
        }
    ]

    // ---------------------------------------------------------------- 导航状态

    /// 顶级高亮项。子菜单显示的就是它的条目——鼠标悬停与键盘移动都改这一个值，
    /// 因此不需要另设 openGroup 之类的第二份状态。
    property int  index: 0
    /// 子菜单高亮项（groups[index].items 的下标）
    property int  subIndex: 0
    /// 键盘焦点当前落在哪一层：false = 顶级，true = 子菜单
    property bool subFocus: false

    /// 某一组里可停留的下标（跳过分隔符）
    function selectableItems(groupIndex) {
        if (groupIndex < 0 || groupIndex >= groups.length) return []
        const items = groups[groupIndex].items
        const out = []
        for (let i = 0; i < items.length; ++i)
            if (!items[i].separator) out.push(i)
        return out
    }

    function firstSelectable(groupIndex) {
        const list = selectableItems(groupIndex)
        return list.length > 0 ? list[0] : 0
    }

    function resetNavigation() {
        index    = 0
        subFocus = false
        subIndex = firstSelectable(0)
    }

    /// 键盘打开（F10）：从第一项开始，且焦点在顶级
    function openByKeyboard() {
        resetNavigation()
        open()
    }

    /// 上下移动。顶级一层只换组，子菜单一层在可选项之间移动。
    /// 两端不循环——菜单里到处乱跳比停住更让人迷惑。
    function step(forward) {
        const delta = forward ? 1 : -1
        if (!subFocus) {
            index = Math.max(0, Math.min(groups.length - 1, index + delta))
            subIndex = firstSelectable(index)
            return
        }
        const list = selectableItems(index)
        if (list.length === 0) return
        let pos = list.indexOf(subIndex)
        if (pos < 0) pos = 0
        subIndex = list[Math.max(0, Math.min(list.length - 1, pos + delta))]
    }

    /// →/Enter：进入子菜单；已在子菜单里则由调用方执行该项
    function enterSub() {
        if (subFocus) return
        subIndex = (selectableItems(index).indexOf(subIndex) >= 0)
                   ? subIndex : firstSelectable(index)
        subFocus = true
    }

    /// ←：退回顶级；已在顶级则关掉整个菜单
    function leaveSub() {
        if (subFocus) { subFocus = false; return }
        close()
    }

    /// Enter / Space
    function activate() {
        if (!subFocus) { enterSub(); return }
        const items = groups[index] ? groups[index].items : []
        const item = items[subIndex]
        if (!item || item.separator) return
        triggered(item.action)
        close()
    }

    function isChecked(item) {
        if (!item.checkable || !item.checkedWhen) return false
        return menu.checkedState(item.checkedWhen)
    }

    /// 顶级第 N 行的纵向偏移（子菜单据此与高亮行对齐）。
    /// 行高 28 + 行间距 1，与 TopRow 保持一致。
    function rowOffset(groupIndex) {
        return Theme.padS + Math.max(0, groupIndex) * 29
    }

    // ---------------------------------------------------------------- 叶子项

    component Entry: Rectangle {
        id: ent
        property string label: ""
        property string shortcut: ""
        property bool   checked: false
        property bool   checkable: false
        property bool   usable: true
        property bool   highlighted: false

        signal picked()
        signal hovered()

        implicitHeight: 26
        radius: Theme.radiusS
        color: (ent.highlighted && ent.usable) ? Theme.hover : "transparent"
        opacity: ent.usable ? 1.0 : 0.35

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 12
            text: ent.checkable && ent.checked ? "✓" : ""
            color: Theme.accent
            font.pixelSize: 11
        }
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            text: ent.label
            color: Theme.text
            font.pixelSize: 12
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: ent.shortcut
            color: Theme.textDim
            font.pixelSize: 11
        }
        MouseArea {
            id: entArea
            anchors.fill: parent
            hoverEnabled: true
            enabled: ent.usable
            cursorShape: Qt.PointingHandCursor
            onEntered: ent.hovered()
            onClicked: ent.picked()
        }
    }

    component TopRow: Rectangle {
        id: topRowItem
        property string label: ""
        property bool   opened: false

        signal hovered()
        signal picked()

        implicitHeight: 28
        radius: Theme.radiusS
        color: topRowItem.opened ? Theme.hover : "transparent"

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: topRowItem.label
            color: Theme.text
            font.pixelSize: 12
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: "▸"
            color: Theme.textDim
            font.pixelSize: 11
        }
        MouseArea {
            id: topArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            // 悬停即展开（桌面菜单惯例）；点击展开一级，方便触控
            onEntered: topRowItem.hovered()
            onClicked: topRowItem.hovered()
        }
    }

    // ---------------------------------------------------------------- 内容

    background: Rectangle {
        color: Theme.panel
        radius: Theme.radiusM
        border.width: 1
        border.color: Theme.border
    }

    contentItem: Column {
        id: topRow
        spacing: 1

        // 焦点落在 Popup 时由它的 contentItem 接收按键：
        // 把键盘处理全部集中在这一层，子菜单不必自己抢焦点
        // （子菜单是独立 Popup，抢焦点会让「← 退回」和「↑↓」的层级判断变复杂）。
        focus: true

        Keys.onDownPressed:  menu.step(true)
        Keys.onUpPressed:    menu.step(false)
        Keys.onRightPressed: menu.enterSub()
        Keys.onLeftPressed:  menu.leaveSub()
        Keys.onReturnPressed: menu.activate()
        Keys.onEnterPressed:  menu.activate()
        Keys.onSpacePressed:  menu.activate()
        // 接管 Esc：默认的 closePolicy 也关菜单，但这里要一并把导航状态复位
        Keys.onEscapePressed: menu.close()

        Repeater {
            model: menu.groups

            delegate: TopRow {
                required property var modelData
                required property int index
                width: menu.width - Theme.padS * 2
                label: modelData.title
                // 单一高亮来源：鼠标与键盘都改 menu.index
                opened: menu.index === index
                onHovered: {
                    menu.index = index
                    menu.subIndex = menu.firstSelectable(index)
                    menu.subFocus = false
                }
            }
        }
    }

    // ---------------------------------------------------------------- 子菜单

    Popup {
        id: submenu
        // 不能写 parent: menu —— Popup 不是 Item，赋给 parent 会报
        // "Unable to assign ... to QQuickItem"。两者同在窗口内容项下，
        // 因此直接用 menu 的坐标换算即可对齐。
        width: 240
        padding: Theme.padS
        modal: false
        // 不抢焦点：按键统一由顶级菜单的 contentItem 处理
        focus: false
        closePolicy: Popup.NoAutoClose

        // 嵌套 Popup 的坐标是**相对外层 Popup** 的，不是屏幕/窗口绝对坐标：
        // 写成 menu.x + ... 会把外层的位置重复计入，子菜单整体下移约 menu.y。
        x: menu.width + 2
        y: menu.rowOffset(menu.index)
        visible: menu.visible

        readonly property var group: (menu.index >= 0 && menu.index < menu.groups.length)
                                     ? menu.groups[menu.index] : null

        implicitHeight: subInner.implicitHeight + Theme.padS * 2

        background: Rectangle {
            color: Theme.panel
            radius: Theme.radiusM
            border.width: 1
            border.color: Theme.border
        }

        contentItem: Column {
            id: subInner
            width: submenu.width - Theme.padS * 2
            spacing: 1

            Repeater {
                model: submenu.group ? submenu.group.items : []

                delegate: Loader {
                    required property var modelData
                    required property int index
                    width: subInner.width

                    sourceComponent: modelData.separator ? sepComp : entryComp

                    Component {
                        id: sepComp
                        Rectangle {
                            width: subInner.width
                            height: 9
                            color: "transparent"
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width
                                height: 1
                                color: Theme.border
                            }
                        }
                    }

                    Component {
                        id: entryComp
                        Entry {
                            width: subInner.width
                            label: modelData.label
                            shortcut: modelData.shortcut
                            checkable: !!modelData.checkable
                            checked: menu.isChecked(modelData)
                            highlighted: menu.subIndex === index
                            onHovered: {
                                menu.subIndex = index
                                menu.subFocus = true
                            }
                            onPicked: {
                                menu.triggered(modelData.action)
                                menu.close()
                            }
                        }
                    }
                }
            }
        }
    }

    onOpened: {
        resetNavigation()
        // 打开后把键盘焦点交给顶级列表，F10 打开即可直接 ↑↓
        topRow.forceActiveFocus()
    }
    onClosed: subFocus = false
}
