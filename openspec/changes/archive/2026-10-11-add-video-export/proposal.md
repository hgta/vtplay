## Why

用户拍摄的 4K 视频单文件常达 150MB 以上（实测 `IMG_2327.MP4`：2160×3840 / 60fps / HEVC / 54Mbps / 157MB），无法通过微信等即时通讯方式便捷分享——聊天场景对体积敏感，而原文件的分辨率与码率远超分享所需。当前 vtplay 只能播放，用户必须借助外部工具（格式工厂、HandBrake、网页转码）才能导出，流程割裂。

同时，导出所需的基础元数据（文件名、文件大小、码率、音频参数）在当前 `MediaInfo` 中完全缺失，这也是界面显示"当前视频名称与规格"的前置条件。

## What Changes

- 新增**视频导出/转换**能力：对当前打开的视频按预设转码输出，实时显示进度、速度与预计体积，支持取消与清理。
- 预设覆盖真实分享场景：
  - **微信分享** 720p ≈ 2.5Mbps（1 分钟约 19MB）
  - **微信高清** 1080p ≈ 5Mbps（1 分钟约 38MB）
  - **朋友圈片段** 720p + 限 15 秒
  - **仅换格式** 保持原画质，快速换容器/提升兼容性
  - **自定义** 分辨率/码率手动设置
- 实现方式：调用**外部 `ffmpeg.exe` 子进程**（`QProcess`），应用不链接编码器。导出与播放互相独立，可边播边导。
- 导出产物默认启用 `-movflags +faststart`（moov 前置），微信/网页播放器可边下边播。
- 缩放采用 `scale=-2:<短边>:flags=lanczos`，竖屏/横屏/异形比例自动适配且保证偶数尺寸（H.264 要求）。
- 扩展 `MediaInfo`：新增文件名、文件大小、视频码率、音频采样率/声道/码率；提供展示用格式化（`54 Mbps` / `157 MB`）。
- 探测 `ffmpeg` 可执行文件位置（PATH / 应用同级 / 用户配置）；缺失时给出明确提示与配置入口。

## Capabilities

### New Capabilities

- `video-export`: 视频转码导出——预设定义、命令行构造、进度解析、取消与半成品清理、输出命名与冲突处理、ffmpeg 探测
- `media-metadata`: 媒体元数据模型扩展与展示格式化（文件名、文件大小、码率、音频参数）

### Modified Capabilities

（无：`add-vtplay-mvp` 尚未归档，主 `openspec/specs/` 为空，本变更以新增能力形式引入）

## Impact

**新增代码**
- `src/app/include/vtapp/Exporter.h`、`src/app/src/Exporter.cpp`（`QProcess` 驱动 + `-progress` 解析）
- `src/app/qml/ExportDialog.qml`（预设选择、进度、取消）

**修改代码**
- `src/core/include/vtcore/MediaInfo.h`：新增字段
- `src/core/src/MediaPipeline.cpp`：打开媒体时填充新增字段
- `src/app/include/vtapp/PlayerController.h` / `PlayerController.cpp`：暴露导出命令与进度属性
- `src/app/qml/ControlsBar.qml`：新增"导出"入口（与 `add-ui-shell` 共用改动面）
- `src/app/CMakeLists.txt`：新增源文件与 QML 文件

**运行依赖**
- `ffmpeg.exe` 及其动态库。可执行文件本身仅 0.47MB；配套 DLL 与应用现有 FFmpeg 依赖重合，增量分发成本很小。

**许可证**
- 本机 FFmpeg 为 GPL 构建（`--enable-gpl --enable-libx264 --enable-libx265`）。以子进程方式调用不将 GPL 传染至应用本体，且为未来切换 LGPL 构建保留余地；分发时需随附 ffmpeg 的许可证与源码获取说明（`add-brand-assets` 的"关于/许可"页面承接）。
