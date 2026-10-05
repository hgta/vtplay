# VTPlay

跨平台桌面音视频播放器，直接构建于 **FFmpeg** 之上，主打"快速精准操作 + 简单易用 + 品牌深色荧光绿皮肤"。

> 当前版本：**0.2.0** — 播放/暂停/seek/音量/倍速/全屏 + 深色皮肤；
> 新增**视频导出**（导出为适合微信分享的尺寸与体积）、当前文件名与规格显示、
> 4K 多线程软解与按显示尺寸解码、应用图标与 Windows 版本资源。

## 技术栈

- C++20 + Qt 6（Core / GUI / Quick / QuickControls2 / OpenGL / Multimedia）
- FFmpeg（libavformat / libavcodec / libswscale / libswresample / libavutil），LGPL 动态链接
- 自研媒体管线（不依赖 libVLC / libmpv 等现成播放框架）
- CMake ≥ 3.21 + vcpkg / 系统包

## 目录结构

```
vtplay/
├── CMakeLists.txt
├── vcpkg.json
├── README.md
├── openspec/                       OpenSpec 变更与规格（spec-driven）
└── src/
    ├── core/                       媒体管线（纯 C++，headless 可测）
    │   ├── include/vtcore/
    │   └── src/
    ├── app/                        Qt/QML 壳与控制层
    │   ├── include/vtapp/
    │   ├── src/
    │   └── qml/                    Main.qml / VideoSurface / ControlsBar / Theme
    └── tools/                      headless 自测工具
        └── src/pipeline_check.cpp
```

## 构建

### Windows — MSYS2 / MinGW（无需管理员权限）

```bash
# 一次性：安装 MSYS2 + 工具链（已自动化到 D:\dev\msys64）
curl -sLo D:\dev\msys2-base.tar.xz https://repo.msys2.org/distrib/x86_64/msys2-base-x86_64-20260927.tar.xz
tar -xf D:\dev\msys2-base.tar.xz -C D:\dev
ren D:\dev\msys2-base D:\dev\msys64

# 安装依赖（GCC 16、Qt 6.11、FFmpeg 9、CMake、Ninja，LGPL 默认配置）
D:\dev\msys64\msys2_shell.cmd -mingw64 -no-start -c "pacman -S --noconfirm --needed \
    mingw-w64-x86_64-toolchain \
    mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-pkg-config \
    mingw-w64-x86_64-qt6-base mingw-w64-x86_64-qt6-declarative \
    mingw-w64-x86_64-qt6-shadertools mingw-w64-x86_64-qt6-multimedia \
    mingw-w64-x86_64-ffmpeg"

# 配置 + 构建
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/mingw64
cmake --build build
```

可执行文件：
- `build/src/app/vtplay.exe` — GUI 播放器
- `build/src/tools/vtplay-pipeline-check.exe <file>` — headless 管线自测

### Linux — apt

```bash
sudo apt install qt6-base-dev qt6-declarative-dev qt6-shadertools-dev \
                 libavformat-dev libavcodec-dev libswscale-dev libswresample-dev libavutil-dev \
                 cmake ninja-build

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 运行

⚠️ Windows 下运行需把 MinGW 的 `bin` 目录加到 PATH（让 EXE 能找到 Qt / FFmpeg / libstdc++ 等 DLL）：

```powershell
$env:PATH = "D:\dev\msys64\mingw64\bin;" + $env:PATH
.\build\src\app\vtplay.exe                              # 启动 GUI
.\build\src\tools\vtplay-pipeline-check.exe media.mp4   # headless 自测
```

或直接用仓库根目录的启动脚本（自动设置 PATH，可带媒体文件参数）：

```powershell
.\vtplay.bat                     # 启动 GUI
.\vtplay.bat D:\video\demo.mp4   # 启动并直接播放
```

脚本默认使用 `D:\dev\msys64\mingw64\bin`，可用环境变量 `VTPLAY_MINGW_BIN` / `VTPLAY_EXE` 覆盖。

### 许可证

> ⚠️ 本节描述**实际使用的构建配置**，请勿按其他假设分发。

**应用链接的 FFmpeg：GPL 构建。** 当前开发环境使用 MSYS2 的
`mingw-w64-x86_64-ffmpeg`，其编译配置含 `--enable-gpl --enable-libx264
--enable-libx265 --enable-libmp3lame`（可用 `ffmpeg -version` 查看
`configuration:` 行确认）。因此**链接该库的 vtplay 二进制受 GPL 约束**：

- 分发时须随附完整源码或可获取源码的书面承诺，并保留版权与许可证声明
- 动态链接不改变 GPL 的传染性（与 LGPL 不同）

**导出功能不链接编码 API。** 视频导出通过**外部 `ffmpeg.exe` 子进程**完成
（见 `openspec/changes/add-video-export/design.md` 决策 D1）。以独立进程调用
不构成衍生作品，因此：

- 应用本体与随附的 ffmpeg 各自受其自身许可证约束
- 分发随附的 ffmpeg 时，只需附上该 ffmpeg 的许可证与源码获取方式
- 若将来希望以 LGPL/闭源方式分发应用本体，可换用 LGPL 构建的 FFmpeg
  （需自行编译，不含 x264/x265 等 GPL 组件）

**Qt** 以 LGPLv3 使用（动态链接）。

「关于 VTPlay」页面（见 `add-brand-assets`）将把这些信息呈现在应用内。

### FFmpeg 9 API 适配说明

本项目适配 FFmpeg 9.x：
- `AVChannelLayout` 替代旧的 `av_get_default_channel_layout`
- `swr_alloc_set_opts2` 替代 `swr_alloc_set_opts`
- `frame->ch_layout.nb_channels` 替代 `frame->channels`
- `av_packet_unref` + `av_packet_free` 配合 `avcodec_send_packet` 的所有权契约

## 操作快捷键

| 快捷键 | 功能 |
|---|---|
| `Space` | 播放 / 暂停 |
| `←` / `→` | 快退 / 快进 5 秒 |
| `Ctrl+←` / `Ctrl+→` | 逐帧后退 / 前进 |
| `↑` / `↓` | 音量增 / 减 |
| `F` / 双击视频区 | 全屏切换 |
| `Ctrl+O` | 打开文件 |
| `Ctrl+E` | 导出 / 转换当前视频 |

## 导出 / 转换

对当前打开的视频按预设转码，便于在微信等场景分享（源文件体积通常下降 90% 以上）。
入口：控制条「⬇」按钮或 `Ctrl+E`。

| 预设 | 短边 | 视频码率 | 说明 |
|---|---|---|---|
| 微信分享 | 720 | ≤2.5 Mbps | 适合聊天发送 |
| 微信高清 | 1080 | ≤5 Mbps | 画质更好 |
| 朋友圈片段 | 720 | ≤2 Mbps | 仅前 15 秒 |
| 仅换格式 | 原尺寸 | 不重编码 | 换容器/提升兼容性，极快 |
| 自定义 | 480–2160 | 1–8 Mbps | 手动指定 |

特性：

- **按短边约束缩放**（`scale=-2:S` / `scale=S:-2`），竖屏/横屏/异形比例自动适配，
  且源尺寸已足够小时**不放大**
- 输出 `-movflags +faststart`（moov 前置），微信/网页可边下边播
- 导出在**独立子进程**中执行，播放、seek、切换文件均不受影响，可随时取消
- 同名文件自动追加 `_1`/`_2`，绝不覆盖；取消或失败后不残留半成品
- 预估体积为**上界**（按目标码率计算），实际通常更小

**依赖**：需要可用的 `ffmpeg` 可执行文件。探测顺序为
「用户配置路径 → 应用同级目录（含 `bin/`） → 系统 `PATH`」；
未找到时导出入口置灰并在对话框中说明。

headless 自测（无需 UI）：

```bash
vtplay-export-check --list                                   # 列出预设
vtplay-export-check <file> wechat-share --dry                # 只打印命令与预估
vtplay-export-check <file> wechat-share                      # 真实导出并显示进度
vtplay-export-check <file> wechat-share --cancel-after 4     # 验证取消后无残留
```

## 发布（Windows 便携版）

版本号的唯一来源是 `CMakeLists.txt` 里的 `project(vtplay VERSION x.y.z)`——
它同时注入到程序内（关于页显示）与 exe 的版本资源，打包脚本也从这里读，不另设一处。

```powershell
# 1. 构建
powershell -File D:\dev\msys64\build_and_verify.ps1

# 2. 打包（会自动算依赖闭包、写 qt.conf、附带 ffmpeg）
powershell -File tools\release\make_release.ps1 -QtBin D:\dev\msys64\mingw64\bin

# 3. 验证便携包真的能独立运行（必须做，见下）
powershell -File tools\release\verify_package.ps1 -Media <一个测试视频>
```

产物在 `dist\vtplay-<版本>-win64.zip`。

**为什么打包不是「拷个 exe」**（每一步都对应一个真实踩过的坑）：

| 步骤 | 不做会怎样 |
|---|---|
| `windeployqt --qmldir` | exe 依赖 13 个 DLL，别人拿到直接跑不起来 |
| `--compiler-runtime` + 兜底复制 | 缺 `libstdc++-6` / `libgcc_s_seh-1` / `libwinpthread-1`，双击无反应 |
| 写 `qt.conf` | QML 导入路径指向编译期前缀，换机器报 `module "QtQuick.Controls.Basic" is not installed` |
| 依赖闭包收集 | `windeployqt` 不追**传递**依赖。缺 `zlib1`/`pcre2`/`icu`/`harfbuzz` 以及 FFmpeg 那串编解码库时，加载器在 `main()` 之前就失败——**连日志都不会产生** |
| 随包 `ffmpeg.exe` | 导出功能需要用户自己装 ffmpeg |

**验证判据不是「进程还活着」**：缺 DLL 或缺 QML 模块时，程序可能弹个错误对话框干等，
进程照样存活却一行日志都不写。所以判据是两条硬证据——日志里出现启动行，
且 stderr 里出现 `[audio] t=... status=Playing`（证明解码与音频输出真的跑起来了）。
验证时还会把 `PATH` 剥到只剩 `System32`，否则在开发机上永远测不出缺 DLL。

## 路线图

| 版本 | 内容 |
|---|---|
| v0.1 | 播放/暂停/seek/音量/倍速/全屏 + 深色荧光绿皮肤；多线程解码 + 按显示尺寸解码（4K60 HEVC 实测实时）；音频主时钟同步 |
| **v0.2（当前）** | **视频导出/转换**、当前文件名与规格显示、应用图标与 Windows 版本资源、多线程 4K 软解 |
| v0.3 | 应用外壳（侧栏/菜单）、播放列表与最近打开、状态持久化、截图、窗口置顶 |
| v0.4 | 硬件解码（D3D11VA / VA-API / VideoToolbox）、字幕渲染（libass）、文件关联 |

## 开发说明

- 媒体管线 (`src/core`) 是**纯 C++**，不依赖 Qt UI，可独立编译并通过 `vtplay-pipeline-check` 做 headless 验证。
- 应用壳 (`src/app`) 通过 `PlayerController` 把管线暴露给 QML：状态/命令信号 + 元信息。
- 视觉 token 集中在 `src/app/qml/Theme.qml`（`#D4FF00` 强调色），改一处全应用同步。
- 任何变更前先 `openspec list` 查看当前 OpenSpec 变更提案。