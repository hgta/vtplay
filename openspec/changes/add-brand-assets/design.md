## Context

现有 logo 素材 `微信图片_20260929083819_2_29.png` 是一张 **1080×1080 的 AI 生成展示图**：

```
┌─────────────────────────────┐
│  荧光绿背景（带柔和投影）      │
│   ┌───────────────────┐     │
│   │  黑色圆角方块       │     │
│   │   [V + 播放三角]    │     │  ← 品牌图形（核心）
│   │   VTPlay           │     │  ← 文字：16×16 图标下必然糊
│   └───────────────────┘     │
│                             │
│                 豆包AI生成   │  ← 水印，必须去除
└─────────────────────────────┘
```

界面现状：`Main.qml` 与 `VideoSurface.qml` 用「圆角方块 + 字母 V」临时充当 logo（`Theme.accent` 方块内一个 `Text { text: "V" }`）。

构建现状：`src/app/CMakeLists.txt` 中 `WIN32_EXECUTABLE FALSE`（当初为便于查看控制台日志而设），发布版会附带黑色控制台窗口。工程内**没有** `resources/` 目录、`.rc` 文件或 qrc 资源。

版本信息：硬编码在 `main.cpp` 的启动横幅中（`"0.1.0"`），无 CMake 版本变量，QML 侧无版本属性。

## Goals / Non-Goals

**Goals:**

- `vtplay.exe` 在资源管理器 / 任务栏 / Alt+Tab 中显示品牌图标
- 界面 logo 使用矢量资源，任意尺寸清晰
- Release 构建无控制台窗口；Debug 保留控制台便于排查
- 提供「关于」页面，准确展示版本与许可证信息

**Non-Goals:**

- 安装程序（安装器、文件关联、开始菜单项）→ 属分发范畴，后续单独变更
- 应用内换肤/主题系统
- 官网、宣传物料、启动图

## Decisions

### D1：图标采用「矢量重绘」而非位图抠图

| | 位图抠图 | **矢量重绘（选定）** |
|---|---|---|
| 工时 | 快 | 稍慢（需对齐几何与配色） |
| 16×16 表现 | 边缘灰边、细节糊 | 干净清晰 |
| 可维护性 | 改色/改形需重新处理 | 改一处 SVG 重新导出即可 |
| 真源 | PNG | **SVG（source of truth）** |

以原图的比例与配色为基准（荧光绿 `#D4FF00` 系、近黑底 `#0D0D0D`~`#1A1A1A`）重绘：

- **应用图标**：黑底圆角方块 + 荧光绿「V + 播放三角」，**无文字**
- **界面 logo**：同一图形，可选带 "VTPlay" 字样（顶栏并排显示）

### D2：`.ico` 多尺寸打包

包含 16 / 24 / 32 / 48 / 64 / 128 / 256 七个尺寸。生成链路：

```
vetor SVG ──(resvg/Inkscape/浏览器截图)──▶ 各尺寸 PNG ──(Pillow)──▶ vtplay.ico
```

保留 SVG，`.ico` 视为**构建产物**（可重新生成）。本机已确认 Python 可用；若 Pillow 缺失则回退到位图工具或直接由 SVG 导出多尺寸。

### D3：Windows 资源接入

新增 `src/app/vtplay.rc`：

```
IDI_ICON1 ICON "resources/icons/vtplay.ico"
VS_VERSION_INFO VERSIONINFO
  VALUE "ProductName",     "VTPlay"
  VALUE "FileDescription", "VTPlay Media Player"
  VALUE "FileVersion",     "0.2.0.0"
  ...
```

- 加入 `add_executable` 的源列表即可（MinGW 经 `windres` 自动编译）
- **元数据用英文**：`.rc` 内中文在不同 windres/代码页组合下易乱码，界面内的中文由 QML 提供

### D4：子系统按构建类型切换

```cmake
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(vtplay PROPERTIES WIN32_EXECUTABLE FALSE)  # 保留控制台
else()
    set_target_properties(vtplay PROPERTIES WIN32_EXECUTABLE TRUE)   # 发布无黑窗
endif()
```

**副作用**：Release 下 stdout/stderr 无处可去，现有的诊断日志（版本横幅、`[audio]` 设备与状态、sink 统计）将不可见。

**缓解**（三档）：

1. 环境变量 `VTPLAY_CONSOLE=1` 强制附加控制台（保留排查能力，默认关闭）
2. Release 下日志写入 `QStandardPaths::AppLocalDataLocation/vtplay.log`（轮转保留最近若干次运行）
3. 「关于」页面展示关键运行态信息（音频设备名、采样率、sink 状态、ffmpeg 路径与版本）

### D5：版本信息单一来源

```cmake
project(vtplay VERSION 0.2.0)
target_compile_definitions(vtplay PRIVATE VTPLAY_VERSION="${PROJECT_VERSION}")
```

- C++（启动横幅、关于页）与 QML（`PlayerController::appVersion` 属性）共用
- 替换 `main.cpp` 中硬编码的 `"0.1.0"`

### D6：「关于 / 许可」内容

- 应用：名称、版本、构建时间（`__DATE__ __TIME__`）
- 运行环境：Qt 版本（`qVersion()`）、FFmpeg 版本（`av_version_info()`）
- 许可证区块：
  - 本应用自身
  - Qt（LGPLv3）
  - **FFmpeg：GPL 构建（含 libx264 / libx265）**，若采用外部 `ffmpeg.exe` 方案则说明「随附的 ffmpeg 由其自身许可证约束，本应用以独立进程调用」
  - 源码获取说明（GPL 合规要求）
- 同时修正 `README.md` 中"LGPL 动态链接"与实际不符的表述

## Risks / Trade-offs

- **[重绘图形与原设计存在差异]** → 先出图形草稿供确认配色与形态，定稿后再接入
- **[Release 无控制台导致排查困难]** → 三档缓解（环境变量强制控制台 / 日志落文件 / 关于页展示运行态）
- **[`.rc` 中文编码问题]** → 资源元数据用英文；界面中文由 QML 提供
- **[Windows 图标缓存]** → 更换图标后系统可能仍显示旧图标；开发期需重命名 exe 或清理图标缓存验证
- **[README 许可证表述变更可能影响他人预期]** → 如实说明实际构建配置，并注明后续可切 LGPL 构建的路径
- **[矢量 logo 在极小尺寸（任务栏 16px）细节丢失]** → 应用图标单独出「简化版」几何（去掉过细的描边与阴影），而非直接缩放常规版

## Migration Plan

1. 产出 SVG 草稿（应用图标版 + 界面 logo 版）→ 确认
2. 导出多尺寸 `.ico`，接入 `.rc` 与 CMake
3. 切换子系统配置 + 加 `VTPLAY_CONSOLE` 逃生开关 + 日志落文件
4. 引入 `VTPLAY_VERSION`，替换硬编码
5. 新增「关于」页面（入口在 `add-ui-shell` 的"帮助"菜单）
6. 修正 README 许可证表述

**回滚**：图标与 `.rc` 为附加资源，移除即恢复默认图标；子系统配置可单独回退到 `FALSE`。

## Open Questions

- 顶栏 logo 是否保留 "VTPlay" 文字（空间有限）？倾向：图标 + 文字并排，窗口宽度不足时只显示图标
- 是否需要 `VERSIONINFO` 中的公司/版权字段？需要用户提供署名
- 图标是否要出「深色/浅色」两版以适配任务栏主题？Windows 单独图标通常不做双版，暂不做
