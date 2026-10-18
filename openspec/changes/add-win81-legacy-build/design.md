## Context

目标是一台**无法升级的 Windows 8.1 x64 低配 Intel 笔记本（无独显）**，用途是看片 + 导出修复。

当前主干的加载栈是 Qt 6.11.2 + FFmpeg 9.0.2（MSYS2 共享构建，GPL）+ GCC 16（msvcrt）。实测结论（全部可复现）：

**Qt6（现用）——两处硬导入，进程在 `main()` 之前失败**

| 二进制 | 导入 | Win8.1 上的情况 |
|---|---|---|
| `Qt6Gui.dll` | `d3d12.dll` | 文件不存在（Win10 才有）；`Delay Import Directory` 为空，非延迟加载 |
| `Qt6Core.dll` | `SetThreadDescription` | 函数不存在（Win10 1607 才加） |
| 其余 202 个二进制 | — | 只导入 KERNEL32 / msvcrt / ole32 / WS2_32 等 8.1 已有组件 |

**Qt5.15.19+kde（MSYS2）——同样体检通过**

| 二进制 | 系统依赖 |
|---|---|
| `Qt5Core.dll` | KERNEL32/USER32/ADVAPI32/ole32/MPR/NETAPI32/SHELL32/USERENV/VERSION/WINMM/WS2_32 |
| `Qt5Gui.dll` | d3d11 / dxgi / GDI32 / USER32 / ole32 / KERNEL32 / msvcrt（**无 d3d12**，DirectWrite 动态加载） |
| `Qt5Quick/Qml/Multimedia` | 仅 KERNEL32 / msvcrt / USER32 / ole32 一类 + 自身库 |

两条复现命令（Windows PowerShell，`objdump.exe` 来自 MSYS2 binutils）：

```powershell
# ① 列出某个二进制的全部导入 DLL
objdump -p <dll-or-exe> | Select-String 'DLL Name:'

# ② 查函数级硬导入（Qt6 的第二个坑就在这一层）
objdump -p <dll-or-exe> | Select-String 'SetThreadDescription|D3D12CreateDevice'

# ③ 区分常规导入与延迟导入（延迟加载不算启动阻塞）
objdump -p <dll> | Select-String 'Delay Import Directory'
```

**代码现状的两个关键事实**（决定了"移植"而非"重写"）：

- `src/core` 完全零 Qt（1497 行，纯 FFmpeg + std），解码输出已是 RGBA
- 导出/修复栈 1793 行（`Exporter` 398 / `RestoreRunner` 446 / `FilmDiagnosis` 616 / `RestorePlan` 333）**只依赖 QtCore**，无一处 Gui/Quick

**约束**：

- Qt5 要求带版本号的 QML import（`import QtQuick 2.15`），Qt6 明确拒绝带版本号的 import → **同一份 QML 无法同时服务两者**
- 本构建不带 ANGLE（MSYS2 的 `angleproject` 是可选包），Qt Quick 走桌面 OpenGL
- Qt 5.15 开源分支已停止维护（补丁只在 KDE 等下游）

## Goals / Non-Goals

**Goals:**

- 在那台 8.1 机器上完成一条完整链路：**启动 → 播放（seek/逐帧/倍速/音量）→ 截图 → 导出 → 修复导出（诊断 → 方案 → 预览 → 执行 → 取消）→ 设置**
- 依赖闭包有**可验证**的"不含目标系统缺失组件"门禁，而不是靠"应该能跑"
- 跑不起来时给出**可读原因**（缺哪个组件、需要什么），而不是系统级"无法启动此程序"

**Non-Goals:**

- 不与 Qt6 主干功能同步演进；不改变主干任何行为
- 不支持 Windows 7、32 位、ARM64
- 不做硬件解码、不做 AI 超分、不补"时间段选择"（主干也未实现）
- 不承诺修复导出的耗时（那是"快速档"，见 Open Questions）

## Decisions

### D1 走 Qt5 移植，不重写"无 Qt 外壳"

| 方案 | 成本 | 得到什么 |
|---|---|---|
| **Qt5 移植**（选定） | 3~6 天 | 现有全部界面 + 修复导出 + 诊断 + 预览 |
| 无 Qt 外壳 | 5~8 天 | 朴素窗口；1793 行导出栈要从 QtCore 迁到 std；导出/诊断 UI 需重造 |

**理由**：用户明确需要修复导出，而这个能力的界面与逻辑都已经写好且只依赖 QtCore。重写路径唯一优势是"不依赖 Qt"（内存更省、无 GPU 需求），但在 8.1 已被证明能加载 Qt5 的前提下不成立。

**已否决的第三条路**：给 Qt6 补 `d3d12.dll` 存根。死路——`SetThreadDescription` 是**从 kernel32 按函数名硬导入**的，存根 DLL 只能补"整个文件缺失"，补不了"某个 DLL 里少了函数"，除非改 Qt6Core 的导入表。

### D2 单一源码线（`legacy/win81` 分支），不双轨

QML 的 import 版本号互斥（Qt5 要 `2.15`、Qt6 拒绝），所以**不存在"同一份 QML 同时编两版"**的方案。因此形态是：一次移植、分支 thereafter 基本冻结。

- 主干（Qt6，Win10+）继续正常演进
- `legacy/win81` 只接受 **bugfix 回灌**，不接受新功能
- 修复路径：**先在主干修**，再 cherry-pick 到 legacy（方向单一，避免两边各修一遍产生分歧）

### D3 把"导入表体检"做成门禁，而不是一次性排查

这次能定位到 `d3d12.dll` 不是因为读文档，是因为扫了导入表。这个方法应当固化：

- 门禁内容：扫描产物内所有 `*.exe`/`*.dll` 的 `DLL Name:`，比对"Win8.1 不存在集"（`d3d12.dll`、`d3dcompiler_47.dll`、`ucrtbase.dll`、`bcryptprimitives.dll` …）；再对关键 DLL 扫函数级硬导入（`SetThreadDescription` 等）
- 落点：`tools/release/` 下一个独立脚本，打包脚本末尾调用（tasks 中实现）
- 为什么值得：它不需要 8.1 真机就能提前拦住"发出去的包在目标机加载失败"——这类故障在用户侧表现为"双击没反应"，排查成本极高

### D4 渲染后端：默认软件渲染，"能跑"优先

本构建不带 ANGLE，Qt Quick 走桌面 OpenGL；老 Intel 核显可能只有 GL 1.4，而 Qt Quick 需要 GL 2.0+。

- **默认**：`QT_QUICK_BACKEND=software`（在 `main()` 里按平台设置，早于 `QGuiApplication` 创建）
- 用户可用环境变量强制 `gl` 反向覆盖
- 理由：软件后端在 **SD/720p 内容**上是可接受的（CPU 合成一张 720×480 纹理不构成瓶颈），而不稳定的恰恰是老驱动的 GL 实现。先保证"能放"，再谈"更流畅"
- 附带影响：软件后端下 `QSGSimpleTextureNode` + `createTextureFromImage` 依然成立（现有视频渲染路径不依赖 FBO/GL）

### D5 音频层保持"拉模式"，只换 API

| Qt6 | Qt5.15 | 说明 |
|---|---|---|
| `QAudioSink` | `QAudioOutput` | **拉模式 `start(QIODevice*)` 两边都有** |
| `QMediaDevices::defaultAudioOutput()` | `QAudioDeviceInfo::defaultOutputDevice()` | |
| `QAudioDevice::isFormatSupported/preferredFormat` | `QAudioDeviceInfo` 同名 | |
| `QAudioDevice::description()` | `QAudioDeviceInfo::deviceName()` | Qt5 无 description |
| `QAudioFormat`（`Int16`/采样率/声道） | 同名 | 枚举名一致 |

**关键约束**：必须保持拉模式。主时钟依赖 `PcmDevice::readData` 里"已送出字节数"的回馈（`onAudioBytesPlayed`）；改成 push 模式等于重写一整套时钟同步，风险远大于收益。

### D6 `Controls.Basic` → `Default`：只保功能，不追像素

Qt6 的 Basic 风格在 Qt5 不存在（对应 `Default`）。本项目的界面大量覆写 `background`/`color`/`radius`，所以实际影响比"换风格"听起来小。

- 判据：**文字可见、按钮可点、布局不溢出** —— 这三条不过就是 bug
- 不做像素级对齐（那是无底洞，且没有设计稿可比）

### D7 FileDialog 需要真实重写

| Qt6（`QtQuick.Dialogs`） | Qt5（`QtQuick.Dialogs 1.3`） |
|---|---|
| `selectedFile` | `fileUrl` / `file` |
| `defaultSuffix`、`nameFilters` | `nameFilters`、`selectedNameFilter` |
| `fileMode: SaveFile` | `selectExisting: false` |

影响 3 个文件：`Main.qml`（打开文件）、`ExportDialog.qml`（选输出位置）、`SettingsDialog.qml`。

### D8 三步验收，每步独立可判

| 步 | 判据 | 失败时说明什么 |
|---|---|---|
| ① 能起 | 窗口出现 + 日志有启动横幅 + 无 QML 报错 | 移植本身没通（import/注册/CMake） |
| ② 能放 | 打开文件后有声有画，seek/逐帧/倍速可用 | 渲染后端或音频 API 有问题 |
| ③ 能修 | 诊断出结论 → 预览出图 → 导出跑到结束 → 取消无残留 | 导出栈在 Qt5 下的行为差异 |

### D9 随包 ffmpeg 改为**自建静态单文件**，不用 MSYS2 的共享构建

门禁（D3）第一次在 win81 产物上跑就否了包，失败项**不是 Qt 的**，而是随包 `ffmpeg.exe` 的依赖闭包：

| 二进制 | 硬导入 | 谁带进来的 |
|---|---|---|
| `libSvtAv1Enc-4.dll` | `KERNEL32!SetThreadDescription`（Win10 1607） | ffmpeg 的 `--enable-libsvtav1` |
| `librsvg-2-2.dll` | `bcryptprimitives!ProcessPrng`（Win10） | `--enable-librsvg` |
| `libdovi.dll` | 同上 | `--enable-libplacebo`（Dolby Vision 解析） |

含义：应用本身能起（Qt5 那条线是干净的），但**导出/修复导出一点就废**——`ffmpeg.exe` 在加载器层面就失败，`QProcess` 拿到空输出，用户侧表现是"导出没反应"。

**补文件救不了**：`ProcessPrng` / `SetThreadDescription` 缺的是**函数**不是文件，存根只能补文件。三个库是逐个补还是逐个删都不划算，正确的动作是**不把它们编进去**。

**选定方案**：win81 线自建 ffmpeg，关掉用不上的外部库，静态链接成**一个 exe**：

```
--disable-autodetect --enable-gpl
--enable-libx264 --enable-libx265 --enable-libdav1d --enable-libvidstab
--enable-static --disable-shared --pkg-config-flags=--static
--extra-ldflags=-static --extra-libs=-lgomp
```

- `libx264`/`libx265`：导出必需（`Exporter` 直接写 `-c:v libx264` / `libx265`），**且不是失败项**（8.1 干净）→ 保留
- `libdav1d`：AV1 解码，纯 C、干净 → 保留
- `libvidstab`：修复导出的稳定阶段（`vidstabdetect`/`vidstabtransform`），干净 → 保留；它用 OpenMP，但 `vidstab.pc` 没写 `-lgomp`，所以 configure 要补 `--extra-libs=-lgomp`（否则报 "vidstab >= 0.98 not found"，与版本无关，是链接问题）
- 其余（aom / vpx / svt-av1 / rsvg / placebo / vulkan / ass / freetype / gnutls …）→ 全部不要
- 静态的额外收益：包内二进制从 131 个降下来，`libx265-217.dll`（22 MB）、`librsvg-2-2.dll`（11 MB）等一串伴生 DLL 消失；来源只剩"系统 DLL + 自建 exe"两类，门禁不必再逐条追究来源
- 唯一的动态依赖是 `libgcc_s_seh-1.dll`（GCC 运行时）：`-static` 没能把它吃掉，实测加 `-static-libgcc` 重新链接也不变（`libstdc++.a` 的链接规格又把它带回来）。它在包里本来就随应用一起带，8.1 干净，因此不追加折腾——门禁会如实把"这个 DLL 必须在包内"报出来

| 备选 | 否决理由 |
|---|---|
| 继续用 MSYS2 共享 ffmpeg，只删掉这三个 DLL | 删了就是缺文件，`ffmpeg.exe` 直接加载失败 |
| 只关三个库、保持共享链接 | 仍要随包 `libx264-165` / `libx265-217` / `libdav1d-7`，x265 的 22 MB 无法摊薄；同一份代码，静态版活动件更少 |
| 走 LGPL（去掉 x264/x265） | 导出编码器就没了，等于砍掉本变更的核心用途；GPL 义务与主干现状一致（README「许可证」节） |

**许可证影响**：静态链接意味着 `ffmpeg.exe` 内含 GPL 的 x264/x265 → 该 exe 整体 GPL-2.0-or-later。它以**独立子进程**被调用，不传染应用本体（与主干判断一致）。若 legacy 产物对外分发，需附该 exe 的源码获取方式；`ffmpeg.BUILD.txt` 随包记录版本、configure 行与 sha256。

**构建入口**：`tools/release/build-ffmpeg.ps1`（构建逻辑在 `build-ffmpeg.sh`）→ 产出 `third_party/win81/ffmpeg.exe`。脚本收尾会**自己**把产物丢进一张空目录跑一遍门禁——空目录是关键：门禁有"包内自带同名 DLL 即满足"的豁免，在原地扫等于自欺。

**同一根因还牵出第二个产物**：换掉 `ffmpeg.exe` 之后，整包门禁仍然红——因为**应用自己播放**时加载的仍是 MSYS2 的 `avcodec-63.dll`，而它硬导入 `librsvg-2-2.dll` 与 `libSvtAv1Enc-4.dll`（MSYS2 的 ffmpeg 开了这两个外部库）。也就是说 `vtplay.exe` 会在加载 `avcodec` 时连带拉起它们，**应用启动即失败**——比导出坏掉更致命。

所以这条线需要**两套** FFmpeg：

| 产物 | 谁用 | 配置 | 许可证 |
|---|---|---|---|
| `third_party\win81\ffmpeg.exe` | 导出/诊断的子进程 | 静态，`--disable-autodetect` + x264/x265/dav1d/vidstab | GPL-2.0-or-later |
| `third_party\win81\ffmpeg-shared\bin\*.dll` | 应用链接（解码） | 动态，`--disable-autodetect` + 只留 dav1d；`--disable-avdevice/avfilter/postproc --disable-programs` | LGPL-2.1-or-later（不含 GPL 组件） |

共享库不带 x264/x265：**应用不编码**，编码都在子进程里。副作用是应用本体的许可证反而比主干更宽松（LGPL 动态链接），GPL 只落在独立进程的 `ffmpeg.exe` 上。

应用侧靠 `PKG_CONFIG_PATH` 指到 `ffmpeg-shared\lib\pkgconfig` 切换（`tools/release/build-app.ps1` 负责），打包时依赖闭包的搜索顺序把该目录排在 QtBin **之前**——否则会把 MSYS2 那一套拷回来，且 exe 与 dll 版本不一致。

## Risks / Trade-offs

| 风险 | 缓解 |
|---|---|
| 老 Intel 核显 GL 版本不足，Quick 起不来 | D4：默认软件后端；真机第一步就是验证它 |
| Qt 5.15 开源分支 EOL，无安全补丁 | 记录并接受（内网单机）；需要时换 KDE 下游版本，代价是重新体检导入表 |
| 修复导出在这台机器上是小时级 | 本变更只保证"能跑完、能取消、能看进度"；提速留给"快速档" |
| 分支漂移（legacy 落后主干太多，回灌冲突） | 冻结策略 + 只回灌 bugfix；冲突时以主干为准手工抄 |
| 19 个 QML 机械替换引入回归 | 三步验收 + 现有冒烟；`Default` 风格的差异靠"文字可见/按钮可点"兜底 |
| 软件后端下播放高分辨率会掉帧 | 记录为已知限制；不是本变更的验收项（那台机器的片源是 SD） |

## Migration Plan

无破坏性变更：主干、发布流程、既有产物均不受影响。部署步骤即三步验收：

1. 建 `legacy/win81` 分支，改 CMake/QML/音频层，直到 ① 通过
2. 真机验 ②（含软件后端与 GL 各跑一次，记录 GL 版本）
3. 真机验 ③（用最短片段，导出 + 中途取消）
4. 打包：产物名带平台标记（如 `vtplay-0.3.x-win81-x64.zip`），打包末尾跑导入表门禁

**回滚**：不使用该产物 / 删分支即可，主干零影响。

## Open Questions

1. **"快速档"是否并入本变更**：它决定修复导出在那台机器上是否真可用（`preset slow` + 10bit + 稳定两遍在老 CPU 上可能是数小时）。倾向**另开变更**，本变更不承担。
2. **legacy 产物是否对外分发**：随包 ffmpeg 是 GPL 构建，对外分发需附许可与源码获取方式；仅自用则无此负担。这决定是否需要更新 README/许可证章节。
3. **Qt5 来源**：MSYS2 `5.15.19+kde`（与现有工具链一致、已验证可装）vs 官方 `5.15.2`（更"官方"但需另装 Qt 安装器、且要重新体检）。倾向前者，除非真机上出问题。
4. **真机 Intel 型号与显卡型号**：决定 D4 的默认值是否要改成"先试 GL、失败再退"。目前按"默认软件后端"设计。
