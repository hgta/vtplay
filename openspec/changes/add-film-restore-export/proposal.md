## Why

现有导出能力只解决「**把大文件变小以便分享**」：预设模型是「短边 + 码率上限」，
`scaleFilter()` 明确拒绝放大，编码用 `libx264 -preset veryfast -crf 23` + 硬码率上限。

但老片（DVD 压制为主，也包括录像带转录、旧数字存档）的问题**与现代压缩视频几乎不重叠**：

| 现代压缩视频 | 老片 |
|---|---|
| 块效应、蚊噪、振铃 | 基本没有（胶片/磁带时代没有宏块） |
| —— | **隔行扫描 / 3:2 pulldown 梳状纹**（用户常描述为"锯齿/马赛克"） |
| —— | 抖动、胶片颗粒、褪色、对比度衰减、软 |
| 码率高、分辨率高 | 分辨率低（DVD 720×576 / VHS 352×288） |

对这类素材，现有导出不仅**修不了**，还会主动**破坏**：

- 缩放只允许缩小，720×576 输不出更大的尺寸
- 不加去隔行，梳状纹会被重编码固化；若再放大，梳齿会被当成细节放大
- `--maxrate` 硬上限 + `veryfast` 会把已经脆弱的细节洗掉
- 无法只处理某一段——而老片素材里往往只有几个片段值得保留

结论：需要一条**面向画质修复**的导出路径，而不是复用面向分享的压缩路径。

## What Changes

- 新增**源诊断**：打开媒体后按需分析，输出结构化报告——是否交错 / 是否 3:2 pulldown /
  噪点强度 / 抖动强度 / 黑边 / 亮度与色彩衰减。全部使用随包 ffmpeg 已有滤镜
  （`idet`/`fieldmatch`/`bitplanenoise`/`vidstabdetect`/`cropdetect`/`signalstats`），
  **不引入任何新依赖**。
- 新增**老片修复导出**：由「诊断结果 + 用户意图」**推导**出流水线阶段清单，而不是让用户手选滤镜。
  阶段按固定顺序组装：
  `反胶转/去隔行 → 稳定 → 轻降噪(保颗粒) → 放大 → 加回颗粒 → 锐化 · 色彩 → 高质量编码`
- **放大不再被禁止**：`scaleFilter()` 升级为可组装的滤镜链构造，允许放大；
  仍保留「源已足够时不动」的安全默认。
- 新增**保真档**：`crf 16~18` + `preset slow` + 10bit + `tune=grain`，码率上限变为可选
  （保真档不设上限，让 CRF 决定）——解决"导出反而更糊"。
- 新增**只处理选中时间段**：以播放器当前时间轴选择起止，只导出该段。
- 新增**预览一帧**：在花时间导出前，先渲染同一帧的修复结果供对照。
- 导出执行改为**阶段化流水线**（即使 v1 只有单个 ffmpeg 阶段），
  为后续 AI 超分阶段预留接入位置，避免到时重构。

## Capabilities

### New Capabilities

- `source-diagnosis`: 源质量诊断——交错/pulldown/噪点/抖动/黑边/亮度色彩的检测、
  结构化诊断报告、结果缓存与失效
- `film-restore`: 老片修复导出——由诊断与意图推导阶段清单、滤镜链组装、
  保真与修复档预设、时间段导出、一帧预览、阶段化执行与进度

### Modified Capabilities

（无变更对象：`add-video-export` 尚未归档，主 `openspec/specs/` 为空。
本变更对导出行为的两处修改——**允许放大**（原 design D2 的「不放大」）
与 **`maxrate` 由必设改为可选**（原 design D3）——因此一并作为 `film-restore`
的新能力需求引入，待 `add-video-export` 归档后再行合并。）

## Impact

**新增代码**
- `src/core`（或 `src/app`）：源诊断（`idet`/`vidstabdetect`/`bitplanenoise` 等输出的解析与判定）
- 滤镜链组装器：由诊断 + 意图 → `QStringList` 滤镜链（**纯逻辑，可 headless 测试**）
- 阶段化执行器：阶段清单、阶段内进度、动态权重总进度、取消与临时产物清理

**修改代码**
- `src/app/src/Exporter.cpp`：`scaleFilter()` → 可组装滤镜链，允许放大；`maxrate` 改为可选；
  编码参数随档位变化
- `src/app/include/vtapp/Exporter.h`：导出请求模型扩展（意图档位、时间段、放大目标）
- `src/app/qml/ExportDialog.qml`：新增「提升清晰度」档、强度选择、时间段、预览按钮、诊断摘要
- `src/app/src/PlayerController.cpp`：暴露诊断结果与修复导出命令
- `src/tools/src/export_check.cpp`：新增阶段组装与几何校验的 headless 断言

**运行依赖**
- **无新增**。全部使用随包 ffmpeg 已有滤镜（已实测确认：`fieldmatch` `decimate` `pullup`
  `detelecine` `bwdif` `yadif` `estdif` `idet` `vidstabdetect` `vidstabtransform`
  `bitplanenoise` `cropdetect` `removegrain` `chromanr` `noise` `curves` `eq` `colorlevels` `cas` 均在）
- 可选：`nnedi` 需要 `nnedi3_weights.bin`（约 1MB）。若 v1 采用 `nnedi` 做高质量去隔行则需随包，
  否则退回 `bwdif`（内置，无需数据文件）

**许可证**
- 不引入新的第三方组件，维持现状（随包 ffmpeg 为 GPL 构建，以子进程调用）

**性能**
- 修复档耗时显著高于现有分享档（`preset slow` + 10bit + `vidstab` 两遍），
  需在导出前给出预估并在界面展示；`vidstab` 需要一个中间变换文件，需纳入临时产物清理
