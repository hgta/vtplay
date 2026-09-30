# Tasks: 品牌资源与打包配置

> **与设计的偏差（如实记录）**：设计决策 D1 原计划「矢量重绘」。实际实现改为
> **从源素材提取图形 + 重新合成**：脚本按颜色键（由绿色通道推导 alpha）把裁剪区
> 的深色底变为透明，再将图形居中绘制在圆角深色方块上，以 8 倍超采样后降采样。
> 结果满足「16px 可辨识 / 256px 无锯齿」的要求且可由脚本重复生成，但**不是 SVG**，
> 因此「界面 logo 矢量版」未产出（顶栏仍用占位方块 + "V"）。

## 1. 品牌图形产出

- [x] 1.1 提取品牌图形（V + 播放三角）：在源素材中定位深色圆角方块
      （x=351..1695, y=355..1710）与图形区域，裁剪时**排除 "VTPlay" 文字**
      （16px 下文字必然糊成噪点）
- [ ] 1.2 产出界面 logo 版（矢量，可含 "VTPlay" 字样）——**未做**，顶栏仍为占位标识
- [x] 1.3 合成应用图标版（无文字）：深色圆角方块（取源图底色 `#21221C`）+ 居中图形，
      8× 超采样后降采样，边缘带抗锯齿
- [x] 1.4 建立 `resources/icons/`：源素材 `source-logo.png` + 生成脚本
      `gen_icon.ps1`（均已入库，脚本可重复执行且输出逐字节一致：36,933 字节）
      （未采用 SVG，见上方偏差说明）

## 2. 多尺寸图标

- [x] 2.1 由脚本导出 16/24/32/48/64/128/256 像素 PNG（`resources/icons/vtplay_<size>.png`）
- [x] 2.2 打包为 `resources/icons/vtplay.ico`（PNG 载荷、7 个尺寸，36,933 字节）
- [x] 2.3 校验：16px 与 32px 下品牌图形清晰可辨，256px 无锯齿/无色块
- [x] 2.4 生成过程完全脚本化：`resources/icons/gen_icon.ps1`（入库、参数化、
      仅用标准库 + System.Drawing，无外部依赖）

### 实现要点（备后人查阅）

- 源素材是 AI 展示图：荧光绿背景 + 投影 + 水印 + 文字，**不能直接用**
- 背景色（亮绿）与品牌色（`#D4FF00`）色相接近，**颜色法无法区分**，必须按几何裁剪
- `ColorMatrix` 的约定是 `Matrix[源通道*5 + 目标通道]`、偏移量在**最后一行**：
  「G → A」应写 `Matrix13`，alpha 偏移应写 `Matrix43`。把系数写到 `Matrix31`
  会变成 B→G 而让画面整体泛绿（曾实际踩到）
- 颜色键必须把 `Matrix33`（A→A）置 0，否则源图的不透明 alpha 会让一切保持可见

## 3. Windows 资源接入

- [x] 3.1 `src/app/vtplay.rc.in`：`IDI_ICON1 ICON` + `VERSIONINFO`（元数据用英文，
      避免 windres 代码页问题）
- [x] 3.2 `src/app/CMakeLists.txt` 经 `configure_file` 生成 `.rc` 并加入目标源
      （MinGW 由 windres 自动编译）
- [x] 3.3 验证：exe 内嵌图标可被 `Icon.ExtractAssociatedIcon` 取出，32px 下清晰；
      版本信息读出 `FileVersion 0.2.0` / `FileDescription "VTPlay Media Player"`
- [ ] 3.4 资源管理器实机查看（Windows 图标缓存可能导致仍显示旧图标）——**待用户确认**

## 4. 子系统与诊断能力

- [x] 4.1 按构建类型设置 `WIN32_EXECUTABLE`（Debug=FALSE 保留控制台，Release=TRUE 无黑窗）。
      实测 Release 的 PE Subsystem = 2（GUI）
- [x] 4.2 `VTPLAY_CONSOLE=1` 环境变量在窗口子系统下 `AttachConsole` 附加控制台
- [x] 4.3 **实测确认**：窗口子系统下 stdout/stderr 仍可被父进程重定向捕获，
      因此现有验证脚本（截图/日志采集）无需改动
- [x] 4.4 发布构建将诊断信息写入应用数据目录日志文件：`qInstallMessageHandler`
      同时写 stderr 与 `%LOCALAPPDATA%\VTPlay\VTPlay\vtplay.log`，超 1MB 轮转一次
      （在 `add-ui-shell` 中落地；定位「QML 加载失败导致双击无反应」正是靠它）

## 5. 版本号单一来源

- [x] 5.1 根 `CMakeLists.txt`：`project(vtplay VERSION 0.2.0)`；
      `src/app/CMakeLists.txt` 注入 `VTPLAY_VERSION` 宏与 `.rc` 版本号
- [x] 5.2 `main.cpp` 启动横幅改用 `VTPLAY_VERSION`（带 `#ifndef` 兜底为 `dev`）
- [x] 5.3 `PlayerController` 暴露 `appVersion` / `buildTimestamp` / `qtVersion`，
      并提供 `ffmpegVersionText()`（懒探测并缓存）与 `audioDeviceName()`，供「关于」页使用
- [x] 5.4 `.rc` 的 `VERSIONINFO` 与构建版本一致（同为 `PROJECT_VERSION`）
- [x] 5.5 验证：横幅与 exe 属性均显示 0.2.0，均来自同一处定义

## 6. 关于与许可

- [x] 6.1 `AboutDialog.qml`：品牌标识 + 应用名 + 版本 + 构建时间 + Qt 版本
- [x] 6.2 运行态信息区：转码器路径与版本（首行）、音频输出设备名；无转码器时明确说明
- [x] 6.3 许可证区块：说明本应用以 Qt(LGPLv3) 构建、随附 ffmpeg 为 GPL 构建、
      导出以独立进程调用不构成衍生作品
- [x] 6.4 入口接入菜单「帮助 → 关于 VTPlay」，并提供「复制信息」便于提交问题报告
- [x] 6.5 修正 `README.md` 许可证表述：明确说明当前 FFmpeg 为 GPL 构建、
      链接即受 GPL 约束、导出以子进程调用不构成衍生作品、以及未来切 LGPL 的路径

## 7. 验证

- [x] 7.1 Debug 构建保留控制台（本轮开发即在 Release 下通过重定向验证日志）
- [x] 7.2 Release 双击不弹黑窗（PE Subsystem=2 已确认）
- [ ] 7.3 高 DPI 下顶栏/占位页品牌标识清晰——**待界面 logo 落地后验证**
- [ ] 7.4 「关于」页信息与运行环境一致——**待实现**
