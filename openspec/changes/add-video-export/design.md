## Context

`src/core` 目前是一条**纯解码**管线：`Demuxer`（libavformat）→ `Decoder`（libavcodec）→ `FrameQueue` → 输出层（QSG 纹理 + QAudioSink）。没有任何编码或封装能力。`MediaInfo` 仅含 `url / duration / 分辨率 / 帧率 / codec`，缺少文件名、文件大小、码率等导出与展示都需要的信息。

本机 FFmpeg 9.0.2 是 **GPL 构建**（`--enable-gpl --enable-libx264 --enable-libx265 --enable-libmp3lame`，含 `h264_nvenc/qsv/amf` 硬件编码器），`av*.dll` 已随应用分发（播放需要）。

用户场景的具体数值（实测 `IMG_2327.MP4`）：2160×3840 / 60fps / HEVC / 54Mbps / 157MB，24.3 秒。目标是把这类文件变成微信可以直接发送的体积。

## Goals / Non-Goals

**Goals:**

- 打通「打开视频 → 选预设 → 导出 → 得到可分享文件」的完整路径
- 导出**不阻塞**播放与界面，可边播边导
- 体积与质量可预期：选择预设时即显示预估大小
- 失败可诊断（能看到 ffmpeg 的报错）、可取消（不留半成品）

**Non-Goals:**

- 批量导出队列（先做单任务）
- 自由时间轴裁剪（「朋友圈片段」按前 15 秒近似实现，自由裁剪留待后续）
- 自研 `Encoder`/`Muxer`（不链接编码 API）
- 字幕烧录、多音轨选择、滤镜链自定义

## Decisions

### D1：导出走外部 `ffmpeg.exe` 子进程，而非链接 avcodec 编码

| | A. 链接 avcodec 自研编码管线 | **B. 调用外部 ffmpeg.exe（选定）** |
|---|---|---|
| 新增代码 | 编码器配置 + muxing + 时间戳 + 进度 + 取消 ≈ 500 行 | 拼参数 + 解析进度 ≈ 150 行 |
| 额外分发 | 0 | +0.47MB（`ffmpeg.exe`，DLL 与现有依赖重合） |
| 能力上限 | 自己实现缩放/预设，工作量大 | 全部滤镜、编码器、硬件加速现成 |
| 稳健性 | 容器/时间戳/音画同步问题需自行踩坑 | ffmpeg CLI 是工业级验证过的 |
| 进度反馈 | 自行计算 | `-progress pipe:1` 原生提供 |
| 许可证 | 已 GPL（链接 GPL 构建） | 同等现状，但**进程隔离**为将来换 LGPL 构建留余地 |

选定 B：增量成本极低而能力完整，且给许可证留了退路。

### D2：预设以「短边约束 + 码率」表达，而非固定分辨率

```
-vf "scale=-2:720:flags=lanczos"
```

- `-2` = 短边固定 720、长边按比例自动计算并**保证偶数**（H.264 要求偶数尺寸）
- 竖屏 2160×3840 → 720×1280；横屏 1920×1080 → 1280×720；异形比例自动适配
- `flags=lanczos` 高质量缩放（避免此前遇到的块状伪影类问题）
- **不放大**：源短边已 ≤ 目标时改用 `scale=-2:'min(<short>,ih)'` 或不加滤镜

### D3：码率用 `-crf` + `-maxrate` 组合

纯 `-crf` 在剧烈运动场景体积失控，而用户目标是「能发出去」——体积需要上限：

```
-c:v libx264 -preset veryfast -crf 23 -maxrate 2.5M -bufsize 5M
```

- `crf 23` 保证画质下限；`maxrate` 保证体积上限
- `preset veryfast` 是速度/体积的平衡点（`medium` 体积更小但慢约 3 倍）

### D4：强制 `-movflags +faststart`

moov 前置后微信/网页播放器可边下边播；不加则必须整段下载完才能起播。

### D5：进度解析 `-progress pipe:1 -nostats`

ffmpeg 输出机器可读的键值对流：

```
frame=123        fps=45.6      out_time_us=5120000
speed=2.1x       progress=continue
```

- `out_time_us / 总时长` → 百分比
- `speed` → 速度倍率与 ETA = `(总时长 - out_time) / speed`
- `-nostats` 抑制 stderr 的人眼可读统计，避免噪声干扰报错分析

### D6：输出命名与路径

- 默认与源文件同目录：`<basename>_<预设标签>.mp4`（如 `IMG_2327_微信分享.mp4`）
- 同名冲突时追加 `_1`、`_2`
- 用户可通过保存对话框更改路径；目录记入设置供下次默认

### D7：取消与清理

- `QProcess::kill()` → 等待退出 → 删除半成品文件
- 应用退出时若任务仍在运行：先终止并清理，再退出
- 导出期间禁用"再次导出"，但播放、seek、切文件不受影响

### D8：预估大小

- 预估 = 时长 × (视频码率 + 音频码率) / 8（字节）
- 「仅换格式」预设直接显示源文件大小（不重新编码，体积基本不变）
- 导出过程中若 ffmpeg 报告 `total_size`，用它动态校正显示值

### D9：`ffmpeg` 定位顺序

1. 用户配置路径（`QSettings`）
2. 应用同级目录 `ffmpeg.exe` / `bin/ffmpeg.exe`
3. 系统 `PATH`
4. 均未找到 → 导出入口置灰 + 明确提示（含"去设置"入口）

### D10：`MediaInfo` 扩展在 core 层完成

在 `MediaPipeline::open()` 已遍历 `AVFormatContext` 的位置补字段，避免 UI 层重复解析：

```
fileName, fileSizeBytes, videoBitRate,
audioSampleRate, audioChannels, audioBitRate
```

同时提供格式化辅助（`formatBitrate` → `54 Mbps`、`formatSize` → `157 MB`）供导出对话框与 UI 共用。

## Risks / Trade-offs

- **[随附的 ffmpeg 是 GPL 构建]** → 以子进程调用（非链接）降低传染争议；随附其许可证与源码获取说明（由 `add-brand-assets` 的"关于/许可"页面承接）；未来可换 LGPL 构建
- **[用户机器上没有 ffmpeg.exe]** → 三级探测 + 明确提示 + 配置入口；MVP 阶段在 README 说明
- **[4K 解码 + 编码同时跑，CPU 争抢导致播放卡顿]** → 导出时限制 ffmpeg 线程数（`-threads`）或降低进程优先级；需实测标定
- **[硬件编码质量/兼容性差异]** → MVP 只用 libx264 软编，硬件编码作为后续优化项
- **[长视频导出耗时长]** → 显示 `speed` 与 ETA，支持取消，不阻塞播放
- **[中文路径/文件名]** → `QProcess` 参数用 `QStringList` 传递（交由 Qt 处理编码），**绝不手工拼接命令行字符串**
- **[磁盘空间不足]** → 开始前校验可用空间 > 预估大小 × 1.2

## Migration Plan

1. 先落地 `media-metadata`（`MediaInfo` 扩展），供本变更与 `add-ui-shell` 共用
2. 实现 `Exporter`（探测 → 构造 → 运行 → 进度 → 取消）
3. 接入 `ExportDialog` 与控制条入口
4. 回归验证：导出期间播放不受影响；取消后无残留文件；中文路径可用

**回滚**：导出功能完全独立于播放管线，回滚只需移除入口与源文件，不影响既有播放能力。

## Open Questions

- 导出时是否自动降低 ffmpeg 线程数以保障播放流畅？（需实测：`-threads 4` vs 默认）
- 完成后是否自动打开输出目录？（倾向：提供按钮，不自动）
- 是否需要"导出为 GIF/音频提取"等衍生预设？（暂列入后续）
