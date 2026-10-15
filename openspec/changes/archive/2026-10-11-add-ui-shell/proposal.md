## Why

当前界面（`Main.qml`）只有顶栏一个"打开"按钮，没有任何功能入口，用户找不到导出、截图、播放模式、窗口置顶等操作的位置。

更直接的问题是**看不到正在播放什么**：窗口标题恒为 `"VTPlay"`，界面各处均不显示文件名，全屏时更无从判断当前内容。`MediaInfo` 也未提供文件名与规格字段。

此外还有几处体验与实现问题：

- 播放进度依赖 `Main.qml` 中每 100ms 的 `Timer` 手动触发 `player.positionChanged()` 轮询刷新——既浪费 CPU，进度条也不平滑，属 QML 反模式。
- 窗口位置/大小、音量、播放列表每次启动都重置。
- 没有播放列表与最近打开，切换视频必须重新走文件对话框。
- 没有截图、没有"总在最前"这两个高频低成本的实用功能。

## What Changes

- **应用外壳重构**：顶部精简栏（logo + 文件名 + 菜单按钮 + 窗口按钮区）+ 可一键收起的侧栏（播放列表 / 最近打开），菜单覆盖 文件 / 播放 / 视图 / 工具 / 帮助。
- **当前媒体标识显示**（三处，全屏时也不丢失信息）：
  - 窗口标题：`VTPlay — IMG_2327.MP4`
  - 顶栏信息条：文件名 + 规格（`2160×3840 · 60fps · 54Mbps · 157MB`）
  - 控制条内联文件名（全屏时唯一可见处）
- **进度刷新改为事件驱动**：由 C++ 侧按固定节奏 emit 信号或直接由 QML 属性绑定驱动，移除 100ms 轮询。
- **状态持久化**：窗口几何、音量、最近打开列表（`QSettings`）。
- **新增实用功能**：截图（保存到图片目录并在界面提示）、总在最前（`Ctrl+T`）。
- **播放列表**：侧栏管理当前会话的播放队列，支持点击切换、上下曲、循环模式（不循环/单曲/列表）。
- 顺带修正 README 中与实际不符的许可证表述（实际链接的是 GPL 构建的 FFmpeg）。

## Capabilities

### New Capabilities

- `app-menu`: 应用外壳与导航——顶栏、可收起侧栏、菜单结构、菜单与快捷键的一致性
- `media-display`: 当前媒体的标识与规格展示（标题栏 / 顶栏信息条 / 控制条三处）
- `window-state`: 窗口几何、音量、最近打开等状态的持久化与恢复
- `playlist`: 会话内播放列表（增删、切换、循环模式、与最近打开的关系）

### Modified Capabilities

（无：`add-vtplay-mvp` 尚未归档，主 `openspec/specs/` 为空）

## Impact

**新增 QML**
- `src/app/qml/AppMenuBar.qml`（或 `Drawer` 侧栏）、`InfoBar.qml`、`PlaylistPanel.qml`、`Toast.qml`

**修改 QML**
- `Main.qml`：整体布局重构（顶栏 / 侧栏 / 信息条 / 状态栏）、移除 100ms 轮询、接入快捷键
- `ControlsBar.qml`：新增文件名内联显示、截图/导出/置顶入口
- `VideoSurface.qml`：拖放多文件时进入播放列表

**修改 C++**
- `PlayerController`：新增 `fileName` / `mediaSummary` / `playlist` / `alwaysOnTop` 等属性；新增 `takeScreenshot()`；进度改为事件驱动
- `MediaInfo` 消费（字段由 `add-video-export` 的 `media-metadata` 能力提供，本变更复用）

**依赖关系**
- 与 `add-video-export` 共用 `media-metadata`（文件名/大小/码率）与 `ControlsBar` 的改动面——建议**先落地 `add-video-export` 的元数据部分**，本变更再接入显示。
- 与 `add-brand-assets` 共用窗口/标题栏视觉，但无阻塞关系。
