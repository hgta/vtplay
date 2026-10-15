# Proposal: VTPlay MVP — 基于 FFmpeg 的跨平台音视频播放器

## Why

我们需要一款名为 **VTPlay** 的桌面音视频播放器：支持主流音视频格式、以"快速精准操作"为核心体验、界面简洁易用（深色 + 荧光绿品牌视觉）。同时它必须从底层直接构建于 FFmpeg 之上，以便未来深度定制解码管线、并与 AI 能力（如 whisper 自动字幕）结合。当前仓库为空项目，本提案确立技术选型与首版（MVP）范围。

## What Changes

- 新建 C++ / Qt 6 (QML) 跨平台桌面应用骨架（Windows / macOS / Linux）
- 新建基于 FFmpeg C API（libavformat / libavcodec / libswscale / libswresample）的自研媒体管线 `MediaPipeline`：解封装 → 解码（MVP 软解）→ 音视频同步（音频主时钟）→ 输出
- 新建播放控制层 `PlayerController`：播放/暂停/停止、精准 seek、5s 跳转、逐帧步进、0.5x–2x 倍速、音量
- 新建渲染输出层：视频经 QOpenGLWidget 纹理上传渲染，音频经 QAudioSink 输出（Qt 抹平 WASAPI / PulseAudio / CoreAudio 差异）
- 新建极简播放器 UI（QML）：荧光绿 `#D4FF00` 深色皮肤、播放/暂停、进度条 seek、音量、全屏、文件打开与拖拽
- 预留（MVP 不实现）AI 字幕接口：音频帧统一回调 + 动态字幕轨抽象

## Capabilities

### New Capabilities

- `media-pipeline`: 基于 FFmpeg 的媒体管线——文件打开、解封装、软解码、音视频同步（音频主时钟）、精准 seek、倍速、逐帧步进；音频帧回调接口为未来 AI 字幕预留
- `playback-control`: 播放控制 API 与状态机（Idle → Loading → Playing ⇄ Paused → EOF），供 UI 调用的统一命令接口
- `player-ui`: 极简播放器界面——深色荧光绿品牌皮肤、视频渲染区、控制条（播放/暂停、进度条、音量、倍速、全屏）、文件打开与拖拽、键盘快捷键
- `app-shell`: Qt 6 / QML 应用骨架、CMake 构建、FFmpeg 依赖管理（vcpkg / 系统包）、跨平台打包基础

### Modified Capabilities

（无——全新项目，无既有 spec）

## Impact

- **代码**：仓库从零开始，新增 `src/`（pipeline / control / ui）、`CMakeLists.txt`、依赖清单
- **依赖**：Qt 6（Widgets/Quick/Multimedia 不用，仅 Core/GUI/Quick/OpenGL）、FFmpeg（LGPL 构建配置，避免启用 GPL 组件）、未来 whisper.cpp（不在 MVP）
- **平台**：Windows（vcpkg）、macOS（vcpkg/brew）、Linux（apt/系统包）
- **许可证**：FFmpeg 采用 LGPL 动态链接，闭源分发友好；不启用 GPL 可选组件（x264 等）
- **风险**：音视频同步为首版核心难点，采用"音频主时钟 + 视频 late 丢帧"最简模型控制风险
