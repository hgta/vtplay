## Why

一台**无法升级的 Windows 8.1 x64 低配笔记本**需要用 VTPlay 看片与导出修复。而当前所有发布产物基于 Qt 6.11.2——在这台机器上，进程**在 `main()` 之前就失败**，应用层没有任何补救余地。

实测两个硬导入（`objdump -p`）：

| 二进制 | 导入 | 在 Win8.1 上的情况 |
|---|---|---|
| `Qt6Gui.dll` | `d3d12.dll` | **该 DLL 整个不存在**（Win10 才有），且是常规导入非延迟加载 |
| `Qt6Core.dll` | `SetThreadDescription` | **函数不存在**（Win10 1607 才加） |

同一套体检下，栈里的其余部分是干净的：`avcodec/avformat`、`libstdc++/libgcc/libwinpthread` 只导入 KERNEL32 / msvcrt / ole32 / WS2_32；包内 204 个二进制里**只有 `Qt6Gui.dll` 一个文件**触碰 `d3d12.dll`。

所以问题不是"VTPlay 写得不够兼容"——应用自身的 Win32 调用只有 `AttachConsole`/`AllocConsole`——而是**UI 加载栈的选型**。而同一套体检下，MSYS2 的 Qt 5.15.19+kde（Core/Gui/Qml/Quick/Multimedia）**不含上述两个导入**，因此 Qt5 是唯一能"不重写界面"就达成本目标的路径。

**为什么不重写成"无 Qt 外壳"**：修复导出栈共 1793 行（`Exporter` 398 / `RestoreRunner` 446 / `FilmDiagnosis` 616 / `RestorePlan` 333），全部依赖 QtCore 的 `QProcess`/`QDir`/`QRegularExpression`/`QStorageInfo`；且导出与诊断的界面在 Qt6 版本里已经写好。重写需 5~8 天并复制一套更差的 UI，移植 Qt5 只需 3~6 天且界面全部复用。

## What Changes

- 新增一条 **Qt 5.15 构建线**，服务 Windows 8.1 x64：单一源码线（`legacy/win81` 分支），**不与 Qt6 主干双轨并存**——Qt5 要求带版本号的 import、Qt6 拒绝带版本号的 import，同一份 QML 无法同时服务两者
- QML 适配：19 个文件的版本无号导入补版本号；`QtQuick.Controls.Basic` → `Default`（Basic 是 Qt6 才引入的风格）；`QtQuick.Dialogs` 的 FileDialog 换 Qt5 那套 API（`selectedFile`/`nameFilters` 与 `fileUrl`/`selectExisting` 不同）
- 音频输出层换 Qt5 的 `QAudioOutput`/`QAudioDeviceInfo`（现有 `QAudioSink`/`QMediaDevices`/`QAudioDevice`/`QAudioFormat` 共 29 处，集中在 `AudioOutput.{h,cpp}` 与 `PlayerController.cpp` 的两处设备查询）
- 构建系统：`qt_add_qml_module` + `QML_NAMED_ELEMENT` + `loadFromModule` 换成 qrc + `qmlRegisterType`
- 打包脚本支持 Qt5（`windeployqt5`），产物为独立压缩包；随包 FFmpeg 不变（已实测 8.1 干净）
- 新增**渲染后端降级**：Quick 起不来时退回软件渲染。本构建不带 ANGLE，Qt Quick 走桌面 OpenGL，老核显的 GL 版本不足是现实风险
- **不改动 Qt6 主干的任何行为**

## Capabilities

### New Capabilities

- `os-compatibility`: 目标系统支持矩阵与降级契约——每个构建声明最低系统；依赖闭包中禁止出现目标系统不存在的组件；渲染后端不可用时自动降级；跑不起来时给出可读原因（哪个组件缺、需要什么）而不是静默退出或系统级弹窗

### Modified Capabilities

（无。主干 Qt6 的规格行为不变：本变更只新增一条构建线及其约束，不改变任何既有 capability 的需求。）

## Impact

**代码**：`src/app/qml/*.qml`（19 个）、`AudioOutput.{h,cpp}`、`main.cpp`、`src/app/CMakeLists.txt`
**依赖**：Qt 5.15（MSYS2 `qt5-base`/`qt5-declarative`/`qt5-multimedia` 已实测可装；另需 `qt5-tools` 提供 `windeployqt5`）；FFmpeg 9.0.2 与 GCC 16 运行时不需更换
**打包**：`tools/release/make_release.ps1` 需接受 Qt5 的 windeployqt 与新的产物名
**不在范围内**：AI 超分（`add-ai-upscale`）、修复导出的时间段选择（尚未实现）、Qt6 主干的功能演进

**风险**：
- 本构建不带 ANGLE，Quick 依赖桌面 OpenGL；老 Intel 核显可能 GL 版本不足 → 必须先验证软件后端，再验证 GL
- Qt 5.15 开源分支已停止维护，安全补丁只存在于 KDE 等下游分支（本构建面向单台内网机器，可接受，但需记录）
- 修复导出在这台机器上是**小时级**任务——不属兼容性范畴，但影响这条构建线的实际可用性

**未决**：是否为低配机提供"快速档"（更快 preset + 跳过稳定阶段）。它决定这台机器上修复导出是否真的可用，但不属于兼容性能力，倾向另开变更。

**证据可复现**：`objdump -p <dll> | grep 'DLL Name'`（列导入 DLL）与 `objdump -p <dll> | grep -E 'SetThreadDescription|d3d12'`（查函数级硬导入）。完整复现步骤与两端输出将在 design.md 固化。
