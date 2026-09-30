## Why

三处品牌与分发层面的缺失：

1. **exe 没有图标**。资源管理器、任务栏、Alt+Tab 中 `vtplay.exe` 显示系统默认程序图标，品牌识别完全缺失。现有 logo 素材是一张 AI 生成的**展示图**（带荧光绿背景、投影，右下角还有"豆包AI生成"水印），不能直接用作图标；且图标需在 16×16 下可辨认，原图中的 "VTPlay" 文字在该尺寸必然糊成一团。

2. **发布版会弹出控制台黑窗**。`src/app/CMakeLists.txt` 中 `WIN32_EXECUTABLE FALSE`（当初为便于查看日志而设），双击启动会附带一个控制台窗口，观感很差。

3. **缺少许可信息展示**。应用链接的是 GPL 构建的 FFmpeg，但界面中没有"关于/许可"入口；README 里"LGPL 动态链接"的表述与实际不符。

## What Changes

- **品牌图形资源**：从现有 logo 提取并重绘品牌图形（V + 播放三角），产出
  - 应用图标：黑底圆角方块 + 荧光绿图形，**无文字**，多尺寸 `.ico`（16/24/32/48/64/128/256）
  - 界面 logo：矢量绘制，可含 "VTPlay" 字样，任意缩放清晰
- **Windows 图标资源接入**：`.rc` 资源文件 + CMake 配置，使 exe / 任务栏 / 窗口标题显示应用图标。
- **子系统按构建类型切换**：Debug 保留控制台（便于排查），Release 切换为窗口子系统（无黑窗）。
- **"关于 VTPlay"页面**：应用版本、构建时间、Qt/FFmpeg 版本、许可证声明与源码获取说明；同时修正 README 的许可证表述。

## Capabilities

### New Capabilities

- `brand-assets`: 品牌视觉资源（应用图标、界面 logo）的产出、打包与加载方式
- `app-packaging`: Windows 构建配置（子系统切换、图标资源、版本信息）与"关于/许可"信息展示

### Modified Capabilities

（无：`add-vtplay-mvp` 尚未归档，主 `openspec/specs/` 为空）

## Impact

**新增资源**
- `resources/icons/vtplay.ico`（多尺寸应用图标）
- `resources/icons/vtplay_logo.svg`（界面 logo 矢量图）
- `src/app/vtplay.rc`（Windows 资源脚本：图标 + 版本信息）

**修改**
- `src/app/CMakeLists.txt`：接入 `.rc`、按构建类型设置 `WIN32_EXECUTABLE`、引入资源目录
- `src/app/qml/Main.qml`：顶栏 logo 与窗口图标改用品牌资源
- `src/app/qml/VideoSurface.qml`：占位页 logo 改用品牌资源
- 新增 `AboutDialog.qml`（由 `add-ui-shell` 的"帮助"菜单入口打开）
- `README.md`：许可证表述与实际一致

**外部依赖**
- 位图 → 多尺寸 `.ico` 的转换工具（本机 Python + Pillow 可用；若不可用则回退到纯矢量重绘后导出）

**风险**
- 原素材为 AI 生成的展示图，抠图/重绘需保持品牌一致性——重绘前会先确认配色（荧光绿 `#D4FF00` 系）与几何形态。
