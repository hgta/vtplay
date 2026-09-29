# VTPlay

跨平台桌面音视频播放器，直接构建于 **FFmpeg** 之上，主打"快速精准操作 + 简单易用 + 品牌深色荧光绿皮肤"。

> 当前版本：**0.1.0 MVP** — 仅播放、暂停、seek、音量、倍速、全屏 + 深色皮肤。

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

### FFmpeg 许可证

构建配置使用 **LGPL** 配置（不启用 GPL 组件如 x264），分发时按 LGPL 条款保留版权与许可证声明，并以动态库形式随应用分发。

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

## 路线图

| 版本 | 内容 |
|---|---|
| **v0.1（当前 MVP）** | 播放/暂停/seek/音量/倍速/全屏 + 深色荧光绿皮肤；多线程解码 + 按显示尺寸解码（4K60 HEVC 实测实时）；音频主时钟同步 |
| v0.2 | 播放列表、最近播放、文件关联、倍速音质优化、字幕渲染（libass） |
| v0.3 | 硬件解码（D3D11VA / VA-API / VideoToolbox） |
| v0.4 | AI 自动字幕（whisper.cpp 订阅 AudioFrameObserver）、字幕轨动态管理 |

## 开发说明

- 媒体管线 (`src/core`) 是**纯 C++**，不依赖 Qt UI，可独立编译并通过 `vtplay-pipeline-check` 做 headless 验证。
- 应用壳 (`src/app`) 通过 `PlayerController` 把管线暴露给 QML：状态/命令信号 + 元信息。
- 视觉 token 集中在 `src/app/qml/Theme.qml`（`#D4FF00` 强调色），改一处全应用同步。
- 任何变更前先 `openspec list` 查看当前 OpenSpec 变更提案。