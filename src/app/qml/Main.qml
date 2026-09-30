import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Dialogs
import VTPlay 1.0

ApplicationWindow {
    id: win

    // 初始窗口：优先 1600x900（窗口越大画面越清晰），但**绝不超出屏幕可用区**。
    //
    // 早期版本写的是 Math.max(1280, ...) / Math.max(720, ...)，在小屏 + 150% 缩放下
    // 逻辑可用区只有 1280x672，窗口被顶到 1280x720，底部 48px 落到任务栏之下——
    // 表现为「底部的控制条怎么都不出现」，且进度条拖不到。
    width:  Math.min(1600, Screen.desktopAvailableWidth)
    height: Math.min(900,  Screen.desktopAvailableHeight)
    minimumWidth:  Math.min(880, Screen.desktopAvailableWidth)
    minimumHeight: Math.min(520, Screen.desktopAvailableHeight)
    visible: true
    title: player.windowTitle
    color: Theme.bg

    onFlagsChanged: {
        // 运行时改 flags（如切换置顶）会重建原生窗口，Qt 会先把窗口隐藏，
        // 这里恢复可见性——但必须排除「正在退出」的情形，否则窗口关不掉：
        // 关闭把 visible 置 false，紧随其后的 flags 变化又会把它显示回来。
        if (!visible && !win.shuttingDown) visible = true
    }

    /// 已进入关闭流程：用于抑制上面的可见性恢复
    property bool shuttingDown: false

    /// 始终置顶。
    ///
    /// 刻意**不写成 flags 绑定**：QML 每次重新求值都会 setFlags，而在全屏状态下
    /// setFlags 会把窗口打回普通状态——表现为「按 F 全屏没反应」（实测踩到）。
    /// 因此只在「切换置顶」和「退出全屏」这两个时刻命令式地应用一次。
    function applyPinnedFlag() {
        if (win.shuttingDown || player.fullscreen) return
        const want = Qt.Window | (player.alwaysOnTop ? Qt.WindowStaysOnTopHint : 0)
        if ((win.flags | 0) !== (want | 0)) win.flags = want
    }

    // ===== 视图状态 =====
    property bool sidebarOpen: false

    // 窗口不可见/最小化时降低进度刷新频率（省电：此时用户看不到进度变化）
    function syncActiveState() {
        player.setWindowActive(win.active && win.visibility !== Window.Minimized)
    }
    onActiveChanged: syncActiveState()
    onVisibilityChanged: syncActiveState()
    /// 有弹窗/菜单打开时禁用播放类快捷键：否则在导出对话框里输入路径会触发播放暂停
    readonly property bool modalOpen: appMenu.visible || exportDialog.visible
                                       || shortcutsDialog.visible || aboutDialog.visible
                                       || settingsDialog.visible || fileDialog.visible
    /// 记录「正常态」几何：最大化/全屏时 x/y/width/height 已不是可恢复尺寸
    property rect normalGeometry: Qt.rect(0, 0, 0, 0)
    /// 最近一次截图/导出的路径，供 Toast 的「打开所在文件夹」使用
    property string lastArtifact: ""

    function captureNormalGeometry() {
        if (visibility === Window.Windowed)
            normalGeometry = Qt.rect(win.x, win.y, win.width, win.height)
    }
    onXChanged:      captureNormalGeometry()
    onYChanged:      captureNormalGeometry()
    onWidthChanged:  captureNormalGeometry()
    onHeightChanged: captureNormalGeometry()

    // ===== 顶栏 + 媒体信息条 =====
    // 放在 header 内：加载媒体时自动增高、内容区自动让位，无需硬编码偏移。
    // 全屏时整条收起（把画面还给视频），内容区随之自动铺满。
    //
    // 外层必须是普通 Item：Layout（ColumnLayout）会由布局引擎写入自身的
    // implicitHeight，直接给 Layout 绑 implicitHeight 会被覆盖，导致全屏收不起来。
    header: Item {
        id: headerCol
        // 高度用常量与媒体状态推导，**不要**从子项的 implicitHeight/height 反推：
        // 子项（ColumnLayout 及其布局）会写回自身的 implicitHeight，
        // 形成 "Binding loop detected for property implicitHeight"（实测踩到）。
        implicitHeight: player.fullscreen ? 0
                      : Theme.topBarH + (player.hasMedia ? Theme.infoBarH : 0)
        visible: implicitHeight > 0

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            TopBar {
                id: topBar
                Layout.fillWidth: true
                onMenuRequested: appMenu.visible ? appMenu.close() : appMenu.open()
                onOpenRequested: fileDialog.open()
                onScreenshotRequested: win.takeScreenshot()
                onExportRequested: exportDialog.open()
            }

            InfoBar { id: infoBar; Layout.fillWidth: true }
        }
    }

    // ===== 主体：侧栏 + 视频区 =====
    // 用 anchors 而非 Layout：侧栏宽度要做动画，anchors 下动画更可控。
    Item {
        anchors.fill: parent

        Sidebar {
            id: sidebar
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: (win.sidebarOpen && !player.fullscreen) ? Theme.sidebarW : 0
            // 刻意不设 visible: width > 1 —— 那会让「宽度动画」与「可见性」互相打断：
            // 动画刚离开 0 就触发不可见，动画被中断、属性回退到绑定值，于是来回振荡
            // （实测宽度在 4.4 → 0 → 51.5 之间反复）。始终可见 + clip 即可。
            clip: true
            Behavior on width {
                NumberAnimation { duration: Theme.durNormal; easing.type: Easing.InOutQuad }
            }
            onOpenFileRequested: fileDialog.open()
            onNoticeRequested: function(msg) { toast.show(msg) }
        }

        // ---- 视频舞台：画面 + 错误条 + 控制条 + 提示 ----
        Item {
            id: stage
            anchors.left: sidebar.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom

            VideoSurface {
                id: surface
                anchors.fill: parent
                hasMedia: player.hasVideo || player.hasAudio
                onPointerActivity: {
                    controls.opacity = 1
                    hideTimer.restart()
                }
            }

            // 错误提示
            Rectangle {
                z: 5
                visible: player.errorString.length > 0
                color: Theme.dangerBg
                border.color: Theme.danger
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

            // 控制条：必须位于最上层（z 最高），否则会被视频区的鼠标区域遮挡而点不到。
            // 只通过 opacity 控制显隐（避免 visible/opacity 相互绑定的循环）。
            ControlsBar {
                id: controls
                z: 10
                opacity: 0
                onExportRequested: exportDialog.open()
            }

            Toast {
                id: toast
                z: 20
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 84
                onActionClicked: if (win.lastArtifact.length > 0)
                                     player.openContainingFolder(win.lastArtifact)
            }

            // 鼠标静止 3s 隐藏控制条。
            // 侧栏展开时不隐藏：此时用户在看列表，控制条消失会造成“按钮够不到”的错觉。
            Timer {
                id: hideTimer
                interval: 3000
                repeat: true
                running: true
                onTriggered: {
                    if (!controls.hovered && !win.sidebarOpen && !win.modalOpen)
                        controls.opacity = 0
                }
            }
        }
    }

    // ===== 窗口级拖放：多文件按顺序入队（侧栏与视频区都能接）=====
    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (drop.hasUrls && drop.urls.length > 0) {
                player.addToPlaylist(drop.urls)
                drop.acceptProposedAction()
            }
        }
    }

    // ===== 弹窗 =====
    FileDialog {
        id: fileDialog
        title: "选择媒体文件"
        nameFilters: [
            "视频 (*.mp4 *.mkv *.mov *.avi *.webm)",
            "音频 (*.mp3 *.flac *.wav *.aac *.ogg)",
            "所有文件 (*)"
        ]
        onAccepted: player.openUrl(selectedFile)
    }

    // 多选：一次加入播放列表；只选一个时等价于打开它
    FileDialog {
        id: addFilesDialog
        title: "添加到播放列表"
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            "视频 (*.mp4 *.mkv *.mov *.avi *.webm)",
            "音频 (*.mp3 *.flac *.wav *.aac *.ogg)",
            "所有文件 (*)"
        ]
        onAccepted: player.addToPlaylist(selectedFiles)
    }

    AppMenu {
        id: appMenu
        x: Theme.padM
        y: headerCol.implicitHeight + 4
        checkedState: function(what) {
            switch (what) {
            case "none":        return player.loopMode === "none"
            case "one":         return player.loopMode === "one"
            case "all":         return player.loopMode === "all"
            case "sidebar":     return win.sidebarOpen
            case "fullscreen":  return player.fullscreen
            case "alwaysOnTop": return player.alwaysOnTop
            }
            return false
        }
        onTriggered: function(action) {
            switch (action) {
            case "open":          fileDialog.open(); break
            case "addFiles":      addFilesDialog.open(); break
            case "revealFile":    player.openContainingFolder(player.sourcePath()); break
            case "screenshot":    win.takeScreenshot(); break
            case "export":        exportDialog.open(); break
            case "quit":          win.close(); break

            case "togglePlay":    player.togglePlay(); break
            case "prev":          player.playlistPrevious(); break
            case "next":          player.playlistNext(); break
            case "back5":         player.seekDelta(-5); break
            case "fwd5":          player.seekDelta(5); break
            case "back1":         player.seekDelta(-1); break
            case "fwd1":          player.seekDelta(1); break
            case "stepBack":      player.stepFrame(-1); break
            case "stepFrame":     player.stepFrame(1); break
            case "mute":          player.muted = !player.muted; break
            // 循环模式按「不循环 → 单曲 → 列表」循环切换（一个菜单项代替三个）
            case "cycleLoop":
                player.loopMode = player.loopMode === "none" ? "one"
                                : player.loopMode === "one"  ? "all"
                                : "none"
                break

            case "toggleSidebar": win.sidebarOpen = !win.sidebarOpen; break
            case "fullscreen":    player.toggleFullscreen(); break
            case "alwaysOnTop":   player.alwaysOnTop = !player.alwaysOnTop; break

            case "shortcuts":     shortcutsDialog.open(); break
            case "settings":      settingsDialog.open(); break
            case "about":         aboutDialog.open(); break
            }
        }
    }

    ExportDialog {
        id: exportDialog
        // 「未找到转码器」提示里的「去设置…」：导出页关掉后直接进设置
        onSettingsRequested: settingsDialog.open()
    }
    SettingsDialog { id: settingsDialog }
    ShortcutsDialog { id: shortcutsDialog }
    AboutDialog { id: aboutDialog }

    // ===== 快捷键 =====
    // 全部集中在窗口内定义；菜单里显示的快捷键与此处一一对应。
    // modalOpen 时统一禁用：否则在对话框里打字会误触发播放/seek。
    Shortcut { sequence: "Space";           enabled: !win.modalOpen; onActivated: player.togglePlay() }
    Shortcut { sequence: "Left";            enabled: !win.modalOpen; onActivated: player.seekDelta(-5) }
    Shortcut { sequence: "Right";           enabled: !win.modalOpen; onActivated: player.seekDelta(5) }
    Shortcut { sequence: "Shift+Left";      enabled: !win.modalOpen; onActivated: player.seekDelta(-1) }
    Shortcut { sequence: "Shift+Right";     enabled: !win.modalOpen; onActivated: player.seekDelta(1) }
    Shortcut { sequence: "Ctrl+Left";       enabled: !win.modalOpen; onActivated: player.stepFrame(-1) }
    Shortcut { sequence: "Ctrl+Right";      enabled: !win.modalOpen; onActivated: player.stepFrame(1) }
    Shortcut { sequence: "Up";              enabled: !win.modalOpen; onActivated: player.volume = Math.min(1.0, player.volume + 0.05) }
    Shortcut { sequence: "Down";            enabled: !win.modalOpen; onActivated: player.volume = Math.max(0.0, player.volume - 0.05) }
    Shortcut { sequence: "M";               enabled: !win.modalOpen; onActivated: player.muted = !player.muted }
    Shortcut { sequence: "F";               enabled: !win.modalOpen; onActivated: player.toggleFullscreen() }
    Shortcut { sequence: "?";               enabled: !win.modalOpen; onActivated: shortcutsDialog.open() }
    // F10 唤起菜单（Windows 惯例）。菜单已经打开时 modalOpen 为真，
    // 因此这里额外放行「菜单自身可见」的情形，否则按 F10 关不掉。
    Shortcut {
        sequence: "F10"
        enabled: !win.modalOpen || appMenu.visible
        onActivated: appMenu.visible ? appMenu.close() : appMenu.openByKeyboard()
    }

    Shortcut { sequence: "Ctrl+O";          onActivated: fileDialog.open() }
    Shortcut { sequence: "Ctrl+S";          enabled: !win.modalOpen; onActivated: win.takeScreenshot() }
    Shortcut { sequence: "Ctrl+E";          onActivated: exportDialog.open() }
    Shortcut { sequence: "Ctrl+L";          onActivated: win.sidebarOpen = !win.sidebarOpen }
    Shortcut { sequence: "Ctrl+T";          onActivated: player.alwaysOnTop = !player.alwaysOnTop }
    Shortcut { sequence: "Ctrl+Q";          onActivated: win.close() }
    // F5 = 设置：播放器领域的通用键位（PotPlayer 等），且是单键——
    // 标点组合（Ctrl+,）实测在本项目的 Shortcut 里不触发，换成 F5 更稳。
    Shortcut { sequence: "F5";              onActivated: settingsDialog.open() }
    Shortcut { sequence: "Ctrl+PgUp";       enabled: !win.modalOpen; onActivated: player.playlistPrevious() }
    Shortcut { sequence: "Ctrl+PgDown";     enabled: !win.modalOpen; onActivated: player.playlistNext() }
    Shortcut { sequence: "Escape";          enabled: player.fullscreen && !win.modalOpen
                                            onActivated: player.toggleFullscreen() }

    // ===== 截图 =====
    function takeScreenshot() {
        if (!player.hasMedia) {
            toast.show("请先打开一个视频再截图")
            return
        }
        const path = player.screenshotFilePath()

        // 纯画面（默认）：直接取渲染器里的当前显示帧——不含界面、不含 letterbox 黑边。
        // 比「抓屏再裁黑边」干净：没有 UI 干扰，也不受缩放插值影响。
        if (player.screenshotContent === "frame") {
            if (player.saveCurrentFrame(path)) {
                win.lastArtifact = path
                toast.show("截图已保存：" + path, "打开所在文件夹", 6000)
            } else if (player.hasVideo) {
                toast.show("画面尚未就绪，请稍后再试")
            } else {
                toast.show("该文件没有画面可截取")
            }
            return
        }

        // 含界面：grabToImage 抓「用户实际看到的画面」（含控制条，若它正显示）。
        // 分辨率受当前显示尺寸限制——界面不承诺原始分辨率。
        stage.grabToImage(function(result) {
            if (result.saveToFile(path)) {
                win.lastArtifact = path
                toast.show("截图已保存：" + path, "打开所在文件夹", 6000)
            } else {
                toast.show("截图保存失败：" + path)
            }
        })
    }

    // ===== 状态联动 =====
    Connections {
        target: player
        function onFullscreenChanged() {
            if (player.fullscreen) {
                win.showFullScreen()
            } else {
                win.showNormal()
                // 退出全屏后重新应用置顶（全屏期间刻意不动 flags）
                win.applyPinnedFlag()
            }
        }
        function onAlwaysOnTopChanged() { win.applyPinnedFlag() }
        function onExportFinished(outputPath) {
            win.lastArtifact = outputPath
            toast.show("导出完成：" + outputPath, "打开所在文件夹", 6000)
        }
        function onExportFailed(message, detail) {
            toast.show("导出失败：" + message, "", 5000)
        }
    }

    Component.onCompleted: {
        // 先把窗口交给控制器：几何校验需要外框尺寸（标题栏/边框）
        player.attachWindow(win)
        // 恢复窗口几何；越界（拔插显示器）时 C++ 会返回 valid=false，用默认尺寸
        const g = player.windowGeometry()
        if (g.valid) {
            win.width = g.width
            win.height = g.height
            win.x = g.x
            win.y = g.y
            if (g.maximized) win.showMaximized()
        }
        captureNormalGeometry()
        if (player.alwaysOnTop) applyPinnedFlag()
        if (player.recentIssue.length > 0) toast.show(player.recentIssue)

        // 启动即打印几何：窗口大于屏幕可用区会让底部控制条落到屏幕外（曾实际发生，
        // 表现为「控制条怎么都不出现」）。数值留着便于对照高 DPI 缩放。
        console.log("[ui] screen", Screen.width + "x" + Screen.height,
                    "available", Screen.desktopAvailableWidth + "x" + Screen.desktopAvailableHeight,
                    "dpr", Screen.devicePixelRatio,
                    "window", win.width + "x" + win.height, "@", win.x + "," + win.y)
    }

    onClosing: function(close) {
        console.log("[ui] closing")
        win.shuttingDown = true
        // 只存正常态几何，避免把全屏尺寸存成下次的恢复尺寸
        player.saveWindowGeometry(win.normalGeometry.x, win.normalGeometry.y,
                                  win.normalGeometry.width, win.normalGeometry.height,
                                  win.visibility === Window.Maximized)
        player.persistSettings()
        close.accepted = true
    }
}
