# Tasks: 视频导出 / 转换

## 1. 媒体元数据扩展（前置，供其它变更复用）

- [x] 1.1 扩展 `vtcore::MediaInfo`：新增 `fileName`、`fileSizeBytes`、`videoBitRate`、`audioSampleRate`、`audioChannels`、`audioBitRate`
- [x] 1.2 在 `MediaPipeline::open()` 填充上述字段（文件名从路径提取；大小取文件系统；码率取 `codecpar->bit_rate`，为 0 时回退「容器总码率 − 音频码率」估算）
- [x] 1.3 提供格式化辅助（core 层 `MediaInfo.cpp`，供 UI 与导出共用）：`formatBitrate`（`54 Mbps`/`54Mbps`）、`formatSize`（`158 MB`/`158MB`）、`formatSpecSummary`（`2160×3840 · 60fps · 54Mbps · 158MB`）
- [x] 1.4 `PlayerController` 暴露 `fileName` / `specSummary` / `audioSummary` / `hasMedia` 只读属性（NOTIFY `mediaInfoChanged`）
- [x] 1.5 字段缺失时降级为 `—` 并从摘要中省略（不显示 0/负数）；`close()` 清空元数据避免界面残留；headless 工具 `vtplay-pipeline-check` 输出新增字段用于验证

## 2. 转码器（ffmpeg）探测与配置

- [x] 2.1 实现三级探测：用户配置路径 → 应用同级/`bin` 目录 → 系统 `PATH`（`Exporter::probeTranscoder`，实测命中 PATH 中的 ffmpeg 9.0.2）
- [x] 2.2 探测时执行一次 `-version` 验证可执行性并读取版本字符串（存在但依赖缺失的文件会在此判为不可用）
- [ ] 2.3 未找到时导出入口置灰并给出说明与配置入口（不静默失败）——**界面部分随第 5 组实现**
- [ ] 2.4 提供设置项与 `PlayerController` 属性（`ffmpegPath` / `ffmpegAvailable`）——**随第 5 组实现**

## 3. 导出预设与命令行构造

- [x] 3.1 定义预设数据结构（短边上限、视频码率、CRF、preset、音频码率、是否限时长、是否仅换格式）与 4 个内置预设（自定义预设随第 5 组由界面驱动参数）
- [x] 3.2 实现缩放表达式构造：竖屏 `scale=S:-2`、横屏 `scale=-2:S`，`flags=lanczos`，源短边 ≤ 目标时**不放大**（实测 640×480 源 + 1080 目标输出“(不缩放)”）
- [x] 3.3 组装参数列表（`QStringList`，**不拼命令行字符串**）：`-vf`、`-c:v libx264 -preset veryfast -crf -maxrate -bufsize`、`-c:a aac -b:a`、`-movflags +faststart`、`-progress pipe:1 -nostats`
- [x] 3.4 「仅换格式」走流复制（`-c copy`）；实测 10.3MB 源 → 10.3MB 产物，流保持 h264 1920×1080 + aac，预估偏差 0%
- [x] 3.5 「朋友圈片段」限前 15 秒（`-t 15`）；实测产物时长精确为 15.000000 秒（源 20.93 秒）
- [x] 3.6 编码线程数限制参数（`setEncoderThreads` → `-threads`）可配置，默认不限制

## 4. 导出执行与进度

- [x] 4.1 实现 `Exporter`（`QObject`）：`start()` / `cancel()`，内部持有 `QProcess`
- [x] 4.2 解析 `-progress pipe:1` 输出：百分比、速度、已写字节；完成/失败/取消信号（实测进度 0%→100%，速度 3.45x→1.4x）
- [x] 4.3 输出命名与冲突处理：`<basename>_<预设标签>.mp4`，冲突追加 `_1`/`_2`（实测已存在时输出 `..._1.mp4`）
- [x] 4.4 磁盘空间校验：可用空间 < 预估体积 × 1.2 时阻止并提示（实测返回失败且无残留）
- [x] 4.5 取消：`kill()` → 等待退出 → 删除半成品；应用退出（析构）时同样清理（实测取消后无残留，退出码 3）
- [x] 4.6 保留转码器 stderr 尾部（最多 8 行）作为失败诊断信息
- [ ] 4.7 完成后提供「打开所在文件夹」——**随第 5 组实现**

## 5. 导出界面

- [x] 5.1 `ExportDialog.qml`：源文件信息、预设单选（含预估体积）、输出路径与「浏览」、开始导出（实测对话框正常呈现）
- [x] 5.2 导出中视图：进度条、百分比、速度、已写入大小、取消按钮（实测 19% / 速度 1.09× / 已写入 256 KB）
- [x] 5.3 结果视图：成功（路径 + 打开文件夹）/ 失败（原因摘要 + 可展开详情）
- [x] 5.4 控制条新增「导出」入口（无媒体或转码器不可用时置灰）；快捷键 `Ctrl+E`
- [x] 5.5 转码器不可用时对话框内给出红色提示条，且「开始导出」置灰
- [x] 5.6 自定义预设：短边（480/720/1080/1440/2160）与码率（1–8 Mbps）由界面驱动，预估随参数实时更新

### 实现中踩到的两个 QML 陷阱（已修正，备后人查阅）

- **`Q_INVOKABLE` 不参与 QML 依赖追踪**：把 `player.estimateForPreset(...)` 直接写进绑定，会在启动（尚未加载媒体）时求值一次后**永不刷新**，界面全是 `—`。改为由 C++ 随 `mediaInfoChanged` 下发 `exportPresets`（含 estimate 字段），或在绑定中显式读取一个随媒体变化的属性来建立依赖。
- **自定义属性撞名 final 成员**：`property int currentValue` 与 Qt 6.9+ `ComboBox::currentValue`（final）冲突，`property bool enabled` 与 `Item::enabled` 冲突——都会导致 QML 加载失败或属性覆盖告警。已分别改名 `selectedValue` / `actionEnabled`。
- 另注：`Dialog` 继承自 `Popup`，**不要**定义名为 `reset()` 的函数（会触发 invalid override）。已改名 `resetForm()`。

## 6. 集成与验证

- [ ] 6.1 验证导出期间播放不受影响（含 seek、切文件）——**需 GUI 接入后验证**
- [x] 6.2 验证 4K60 竖屏源导出「微信分享」得到 720×1280 且体积显著下降（实测 2160×3840 / 158MB → **720×1280 / 6.1MB，↓96%**；h264 1.96Mbps + aac 128kbps，ffprobe 校验可播放；耗时 18.5s，约 1.3x 实时）
- [x] 6.3 验证中文文件名与中文路径全流程可用（输出 `IMG_2327_微信分享.mp4` 写入、探测、ffprobe 均正常；参数经 `QStringList` 传递不过 shell）
- [x] 6.4 验证取消后无残留文件（工具内自检 + 脚本外部独立确认：取消于 26.8% 时无残留，退出码 3）
- [x] 6.5 验证同名文件冲突自动改名（预置同名文件后输出 `test1080_微信分享_1.mp4`）
- [ ] 6.6 实测导出时的 CPU 争抢情况，标定 `-threads` 默认值
- [x] 6.7 导出产物可边下边播（`-movflags +faststart` 生效：moov@36 位于 mdat 之前）
