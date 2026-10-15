# Design: VTPlay MVP

## Context

仓库为空项目，从零构建。前期已完成播放内核与外壳的选型探索（详见下方"决策记录"），结论：

- **内核**：不使用 libVLC / libmpv 等现成播放框架，而是直接基于 **FFmpeg C API** 自研媒体管线，满足"从更底层控制音视频、未来自定义解码与 AI 结合（自动字幕等）"的诉求。
- **外壳**：**Qt 6 + QML（C++）**，跨 Windows / macOS / Linux；Qt 同时承担窗口、音频输出（QAudioSink）、视频渲染（QOpenGLWidget）的平台差异抹平。
- **品牌视觉**：VTPlay logo 为荧光绿（约 `#D4FF00`）+ 深色底 + 大圆角，UI 采用同风格深色主题。
- **MVP 原则**：第一版刻意做减法——只做"播放、暂停、seek、音量、倍速、全屏 + 深色皮肤"，跑通自研管线；播放列表、硬解、字幕 UI、AI 全部推迟。

**播放内核选型对比（探索结论记录）**：

| 维度 | libVLC | libmpv | FFmpeg 自研（选定） |
|---|---|---|---|
| 底层控制力 | 低（播放器级封装） | 中 | **高（解封装/解码/滤镜全链路可编程）** |
| 上手难度 | 低 | 中 | 高（需自研同步/seek/输出） |
| 格式支持 | 极广 | 极广 | 取决于构建的 decoder 集（主流格式足够） |
| AI 扩展 | 差（难取裸帧/裸音频流） | 中 | **好（解码帧/音频 PCM 直接可得，whisper.cpp 可订阅）** |
| 许可证 | LGPLv2 | GPLv2+（可构建 LGPL） | LGPL（需注意构建配置不含 GPL 组件） |
| 结论 | 开箱即用但不可控 | 平衡但受限于其抽象 | **符合"底层控制 + AI"长期诉求** |

**外壳技术栈对比（探索结论记录）**：

| 技术栈 | 结论 |
|---|---|
| C++ / Qt 6 + FFmpeg | **选定**：FFmpeg 零绑定损耗；Qt 抹平音频/渲染/窗口三平台差异；whisper.cpp 同语言直链 |
| Rust + ffmpeg-next + winit/cpal | 绑定层遇冷门 API 会掣肘"底层控制"诉求；三件套平台坑多 |
| Python + PyAV + PySide6 | 原型快但性能上限低（GIL、4K 掉帧），不适合最终形态 |
| C# WPF + LibVLCSharp | Windows 最顺但不跨平台、且绑定 libVLC 与内核诉求冲突 |
| Electron / Tauri | 嵌入原生解码器复杂，包体/工程性问题，与"底层控制"相悖 |

## Goals / Non-Goals

**Goals:**

- 自研 FFmpeg 管线：打开主流格式（MP4/MKV/MOV/AVI/WebM/MP3/FLAC/WAV/AAC…）、软解、音视频同步播放
- 精准操作：帧级 seek（关键帧回退 + 前向解码到目标 PTS）、逐帧步进、0.5x–2x 倍速
- 极简 UI：深色 + 荧光绿品牌皮肤，控制条 + 进度条 + 音量 + 全屏 + 快捷键 + 拖拽打开
- 跨平台：Windows / macOS / Linux 一套 CMake + vcpkg/系统包依赖
- 架构可扩展：管线与 UI 解耦、音频帧回调接口预留（AI 字幕）、字幕轨抽象预留（动态轨）

**Non-Goals:**

- 硬件解码（接口按可上传纹理的帧设计，v2 加 `av_hwdevice_ctx` 不改架构）
- 播放列表、播放记忆、多音轨/字幕轨切换 UI、截图、画中画、置顶
- 字幕渲染（libass）与 AI 自动字幕（仅留接口）
- 网络流播放、光盘、投屏

## Decisions

### D1. 分层架构（管线与 UI 严格解耦）

```
┌──────────────────────────────────────────────────┐
│  UI 层 (QML, 深色+荧光绿皮肤)                       │
├──────────────────────────────────────────────────┤
│  PlayerController 控制层（状态机）                  │
│  Idle → Loading → Playing ⇄ Paused → EOF          │
├──────────────────────────────────────────────────┤
│  MediaPipeline 核心层（纯 C++，不依赖 Qt UI）        │
│  Demuxer(libavformat) → Decoder(libavcodec)       │
│      → A/V Sync(音频主时钟) → VideoOut / AudioOut   │
├──────────────────────────────────────────────────┤
│  未来扩展层（仅接口）：AICaption(whisper.cpp)、      │
│  SubtitleTracks(动态轨)、libavfilter 自定义滤镜      │
└──────────────────────────────────────────────────┘
```

- `MediaPipeline` 用纯 C++ + std 线程，仅输出层（QOpenGLWidget/QAudioSink）接触 Qt，可单独 headless 自测（解码统计/帧 dump），也为未来服务化留路。
- 替代方案（管线整体做成 QObject/Qt 信号槽驱动）被否：Qt 依赖渗入核心层，阻塞 headless 与复用。

### D2. 音视频同步：音频主时钟 + 视频 late 丢帧

- 以音频设备已播放的字节数折算的 PTS 为主时钟；视频帧到达时比较 PTS：早到→按差值 sleep/重排，晚到→直接丢弃追赶。
- 倍速通过分别设置音频 tempo（libswresample 或按采样率喂入加速 PCM）与视频按时钟变速渲染实现，MVP 先保证 0.5x–2x 可用，不追求完美音质。
- 替代方案（视频主时钟、外部系统时钟）被否：音频丢帧比视频丢帧感知强烈得多。

### D3. 精准 seek 算法

```
seek(t) → avformat_seek_file(目标t所在关键帧之前的最近关键帧 kf)
        → 从 kf 向前解码并丢弃帧，直到 pts >= t
        → 恢复播放 / （逐帧模式下停在该帧）
```

- MVP 用该通用算法保证 ≤1 帧误差；大间隔流（如部分 TS）可能出现短暂黑屏等待，可接受。

### D4. 渲染与音频输出

- 视频：软解得到 `AVFrame`（YUV/RGB）→ libswscale 转 RGBA → 上传 QOpenGLWidget 纹理（着色器 YUV 直转可作 v2 优化）。
- 音频：libswresample 统一重采样为 S16/立体声/48kHz → QAudioSink 拉取播放；QAudioSink 的 `bytesFree` + 已写入字节数即主时钟来源。
- 替代方案（SDL2 输出）被否：多引一个库，Qt 已够用。

### D5. 线程模型

```
[Demux 线程] ──packet 队列──▶ [Decode 线程 xN] ──frame 队列──▶ [渲染/音频线程]
      ▲                                                        │
      └──────────── PlayerController 命令(seek/pause/rate) ◀─────┘
```

- 有界队列 + 条件变量；队列水位触发 demux 背压；seek 采用"flush 全队列 + 置 discard 标志"统一处理。
- 避免 Qt 主线程做任何解码工作；UI 仅消费状态信号。

### D6. AI 字幕预留接口（MVP 只定义，不实现）

- `MediaPipeline` 暴露 `AudioFrameObserver` 回调（PCM + pts），whisper.cpp 未来订阅即可获得实时音频流。
- 字幕模型按"多轨 + 动态轨"抽象：外部 srt、内嵌轨、AI 生成的虚拟轨同一接口，UI 无需感知来源。

### D7. 依赖与构建

- CMake ≥ 3.21 + Qt 6（Core/GUI/Quick/QuickControls2/OpenGL）+ FFmpeg（LGPL 构建，禁用 GPL 组件）。
- 依赖获取：Windows/macOS 用 vcpkg（`ffmpeg` LGPL 默认配置），Linux 用 apt 系统包；锁定版本写 README。
- 打包：`windeployqt/macdeployqt/linuxdeployqt`（或 CPack），MVP 只保证可运行产物。

### D8. 视觉规范（源自 logo）

| Token | 值 |
|---|---|
| 主背景 | `#0D0D0D` / 面板 `#1A1A1A` |
| 强调色 | `#D4FF00`（进度条、播放态、焦点） |
| 圆角 | 控件 8–12px，窗口 16px |
| 字体 | 系统无衬线，时间码等宽 |
| 动效 | 悬停轻微缩放、进度 glow（QML 实现） |

## Risks / Trade-offs

- [音视频同步首版不稳（音画漂移、高倍速失真）] → 只实现"音频主时钟 + 丢帧"一种模型，先跑稳；预留时钟抽象便于替换。
- [FFmpeg 各平台构建/版本差异] → vcpkg 清单锁版本；CI 三平台矩阵（v2 再上）。
- [QML 做不出品牌质感] → 视觉 token 集中定义（ singleton），控件用 Qt Quick Controls 2 自定义样式。
- [软解 4K/HDR 高码率掉帧] → MVP 定位主流 1080p 场景；硬解在 v2 通过 `av_hwdevice_ctx` 接入，帧输出接口已按纹理上传设计，无需重构。
- [C++ 工程量大于预期] → 分两阶段验收：先 headless 管线自测（解码帧数/pts 连续性统计），再接 UI。
- [帧级 seek 在极端流上慢] → 接受首版通用算法；记录 badcase，v2 按容器/编码加索引优化策略。

## Open Questions

- 倍速下音频处理选型：libswresample tempo 滤镜 vs 简单重采样喂入（实现时以听感与复杂度定）
- 是否在 MVP 中保留"上次播放位置"（当前划为 Non-Goal，若用户要求可提级）
- macOS 上 QAudioSink 与 CoreAudio 的延迟差异是否影响主时钟精度（实现阶段实测）
