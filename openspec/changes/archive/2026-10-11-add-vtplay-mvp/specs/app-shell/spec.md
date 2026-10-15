# app-shell

## ADDED Requirements

### Requirement: CMake 构建体系
项目 SHALL 使用 CMake（≥3.21）组织构建，模块划分为 `core`（MediaPipeline 纯 C++）、`app`（Qt/QML 壳与 UI），支持 Windows / macOS / Linux 三平台构建。

#### Scenario: 全新克隆构建
- **WHEN** 开发者克隆仓库并按 README 配置依赖后执行 CMake 构建
- **THEN** 三平台均产出可运行的 vtplay 可执行文件

### Requirement: FFmpeg 依赖管理（LGPL 合规）
项目 SHALL 通过 vcpkg（Windows/macOS）与系统包（Linux）获取 FFmpeg，使用 LGPL 构建配置（不启用 GPL 组件），以动态链接方式集成；依赖版本在清单中锁定并在 README 记录。

#### Scenario: 许可证合规检查
- **WHEN** 审视构建配置与分发产物
- **THEN** FFmpeg 以 LGPL 动态库形式随应用分发，未链接任何 GPL 组件

### Requirement: Qt 6 应用骨架
应用 SHALL 基于 Qt 6（Core/GUI/Quick/QuickControls2）+ QML 构建，自定义标题栏可后续迭代，MVP 使用系统标题栏。

#### Scenario: 应用启动
- **WHEN** 用户启动 vtplay
- **THEN** 主窗口在 2 秒内呈现，应用可正常打开媒体文件

### Requirement: 错误与边界处理
应用 SHALL 对管线错误、文件不可读等异常给出用户可见提示（消息条/弹窗），任何情况下不崩溃。

#### Scenario: 打开不存在文件
- **WHEN** 用户打开的文件路径已失效
- **THEN** UI 显示错误提示，应用保持在 Idle 状态可继续使用
