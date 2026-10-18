## 1. 前置：分支、工具链与真机信息

- [x] 1.1 从当前 `main` 建出 `legacy/win81` 分支（设计 D2：单一源码线，不与主干双轨）
- [x] 1.2 装 `qt5-tools`（`windeployqt5` 已在位）。**额外还需要两个包**：`qt5-quickcontrols2`（`QQuickStyle` 与 Controls 2）与 `qt5-quickcontrols`（**QtQuick.Dialogs 的宿主包**，缺它会报 "module QtQuick.Dialogs is not installed"）
- [ ] 1.3 **待真机**：记录 Intel CPU 型号、显卡型号、驱动版本、OpenGL 版本（`wmic cpu get name` / `wmic path win32_VideoController get name,driverversion`）
- [ ] 1.4 **待真机**：用现有 0.3.0 产物确认失败现象（预期：缺 `d3d12.dll` 或 `SetThreadDescription`），作为基线证据
- [ ] 1.5 **待真机**：用随包 `ffmpeg -benchmark -i <片子> -f null -` 记录解码余量（`speed=`），确定"能放"这条线是否成立

## 2. 第一步验收：能起

- [x] 2.1 `src/app/CMakeLists.txt`：`qt_add_qml_module` 换成 qrc（新增 `src/app/qml.qrc` + **手写 `src/app/qmldir`**）。qmldir 不是可选项：`Theme` 是 `pragma Singleton`，Qt5 必须在 qmldir 里写 `singleton Theme 1.0 Theme.qml`，否则所有 `Theme.xxx` 未定义
- [x] 2.2 `VideoRenderer.h`：去掉 `QML_NAMED_ELEMENT`（Qt5 无此宏）；类型注册沿用 `main.cpp` 里已有的 `qmlRegisterType(...,"VTPlayCore",1,0,"VideoRenderer")`
- [x] 2.3 `main.cpp`：`loadFromModule` → `engine.load(QUrl("qrc:/VTPlay/Main.qml"))`，根对象为空的显式报错保留
- [x] 2.4 19 个 QML 文件补版本号（实测 46 行；脚本按整行精确匹配，未误伤 `import VTPlay 1.0`）
- [x] 2.5 `QtQuick.Controls.Basic` → `Controls 2.15` + `QQuickStyle::setStyle("Default")`；无残留
- [x] 2.6 FileDialog 三处改写（`selectedFile`→`fileUrl`、`fileMode:OpenFiles`→`selectMultiple:true`+`fileUrls`、`fileMode:SaveFile`→`selectExisting:false`）。**另有两个原计划没料到的坑**：参见第 7 节的「Dialog 类型冲突」与「FolderDialog 不存在」
- [x] 2.7 首次构建通过；开发机启动：窗口出现、日志有启动横幅、无 QML 报错（已截图确认界面与视频画面渲染正常）

## 3. 第二步验收：能放

- [x] 3.1 `main.cpp` 在 `QGuiApplication` 之前设置 `QT_QUICK_BACKEND=software`，环境变量优先（用户可 `=gl` 反向覆盖）
- [x] 3.2 `AudioOutput`：`QAudioSink`→`QAudioOutput`、`QMediaDevices`→`QAudioDeviceInfo`、`stateChanged` 信号随类改；**拉模式 `start(QIODevice*)` 保持不变**。另需改 `QAudioFormat` 的写法（见第 7 节）
- [x] 3.3 `PlayerController::audioDeviceName()`：`QMediaDevices::defaultAudioOutput()` → `QAudioDeviceInfo::defaultOutputDevice()`，`description()` → `deviceName()`
- [ ] 3.4 **待真机**：有声有画 + seek / 逐帧 / 倍速 / 音量逐项验证。开发机已用 10s 测试片验证：`status=Playing`、音频每秒约 192 KB 稳定增长、画面正常（截图留证）
- [ ] 3.5 **待真机**：记录实际后端；若 GL 可用再跑 `QT_QUICK_BACKEND=gl` 对照，据此决定默认值是否调整（回填 design D4）

## 4. 第三步验收：能修（导出与修复导出）

- [ ] 4.1 分享导出跑通：预设选择、体积预估、进度、取消后无残留（headless `vtplay-export-check --dry` 已通过：预设/命令行/体积预估正确，ffmpeg 9.0.2 被识别；**UI 内实跑待真机**）
- [ ] 4.2 诊断跑通：结论（隔行/pulldown/噪点/抖动/黑边/色彩）正确显示（`vtplay-diagnose-check --selftest` 84 项全过、真实文件诊断可跑出结论；**界面展示待真机**）
- [ ] 4.3 预览一帧跑通：点完出图，且不含稳定效果时标注正确
- [ ] 4.4 修复导出跑通：完整方案 + 总进度 + 阶段（估算）三处显示正常；中途取消能终止子进程并清理临时目录
- [x] 4.5 解析 ffmpeg 输出改用 UTF-8（实测是 **9 处**，不是 6 处：Exporter 3、RestoreRunner 4、FilmDiagnosis 2）——`fromLocal8Bit` 在中文系统按 GBK 解 UTF-8 输出，报错会变乱码，直接违背「运行环境问题必须可诊断」。`main.cpp` 解析 `argv` 仍保留 `fromLocal8Bit`（那是 ANSI 参数，不是 UTF-8）

## 5. 依赖闭包门禁与打包

- [x] 5.1 新增 `tools/release/check-target-imports.ps1`：扫描产物内全部 `*.exe`/`*.dll` 的 `DLL Name:`，比对 Win8.1 缺失集（`d3d12.dll` 等）。**首跑就否了包，且失败项不是 Qt 的**——见 5.7
- [x] 5.2 同一脚本增加函数级检查（`SetThreadDescription` 等），失败时输出"哪个文件导入了什么"。实现上不解析 columns，而是在「当前 DLL 的导入区」里正则匹配函数名——`objdump` 的列数随按名/按序数导入而变
- [x] 5.3 在 `make_release.ps1` 末尾调用该门禁；`-QtBin` 支持 Qt5（`windeployqt5`）并按需切换 Qt 版本
- [x] 5.4 产物命名带平台标记（`vtplay-<ver>-win81-x64.zip`），`qt.conf`/依赖闭包逻辑沿用
- [ ] 5.5 真机验收发布包：把 PATH 剥到只剩 System32，确认能独立启动并播放（复用 `verify_package.ps1` 的做法）。**开发机已通过**：`startup PASS` + `play PASS`（`[audio] t=5s status=Playing`），待 8.1 真机复跑
- [ ] 5.6 门禁自检：故意塞一个导入 `d3d12.dll` 的二进制，确认脚本能失败并指出文件（证明门禁不是形同虚设）
- [x] 5.7 自建 win81 用的**静态单文件** `ffmpeg.exe`（`tools/release/build-ffmpeg.ps1` + `build-ffmpeg.sh`）：`--disable-autodetect` + x264/x265/dav1d/vidstab，产物落 `third_party/win81/`（见 design D9）。原因：MSYS2 的共享构建闭包里 `libSvtAv1Enc-4` 硬导入 `SetThreadDescription`、`librsvg`/`libdovi` 硬导入 `ProcessPrng`，导出在 8.1 上必然加载失败
- [x] 5.8 `make_release.ps1` 按构建线分流随包 ffmpeg：win81 只接受自建版（缺失即**失败**，不回退到 MSYS2 那个），主干 Qt6 沿用 MSYS2 构建；`ffmpeg.BUILD.txt`（版本/configure 行/sha256）随包
- [x] 5.9 重打 win81 包并跑门禁：二进制 **116 → 85**，失败项 **0**，包体积 97.2 → **66.4 MB**；`verify_package.ps1`（PATH 剥到 System32）在开发机通过
- [x] 5.10 应用本体也改用自建**共享库**（`tools/release/build-ffmpeg.ps1` 的第二个产物 + `tools/release/build-app.ps1`）：否则 `avcodec-63.dll` 仍会拉起 `librsvg`/`libSvtAv1Enc`，**应用启动即失败**（见 design D9 后半）

## 6. 文档与收尾

- [ ] 6.1 README 增「构建线与系统要求」一节：主干（Qt6 / Win10 1809+）与 legacy（Qt5 / Win8.1 x64）的关系、各自产物名
- [ ] 6.2 把 design 里的两条 `objdump` 复现命令与两端结果表固化进 README 附录（以后有人问"能不能支持 Win7/8.1"直接引用）
- [ ] 6.3 注明 Qt 5.15 开源分支已 EOL 的事实与影响（安全补丁不跟进）
- [ ] 6.4 确认待决项：① "快速档"另开变更 ② legacy 产物是否对外分发（涉及随包 GPL ffmpeg 的许可义务）

## 7. 移植中实测到的 Qt5 差异（原计划未料到）

每条按「现象 → 原因 → 处理」记录，便于将来重做或回灌时直接查：

1. **`module "QtQuick.Dialogs" is not installed`**
   原因：QtQuick.Dialogs 的 QML 插件不在 `qt5-declarative` 里，而在 **`qt5-quickcontrols`**（Qt Quick Controls 1）包里。
   处理：装 `mingw-w64-x86_64-qt5-quickcontrols`。

2. **`Cannot assign to non-existent property "footer"`（ExportDialog 的根 Dialog）**
   原因：Qt5 的 `QtQuick.Dialogs 1.3` **自己也导出一个 `Dialog` 类型**，后导入者遮蔽 `QtQuick.Controls` 的 `Dialog`。Qt6 的 Dialogs 已移除该类型，所以主干上从未暴露。
   处理：含 Dialogs 的三个文件改为 `import QtQuick.Dialogs 1.3 as NativeDialogs` 并用 `NativeDialogs.FileDialog` 显式限定——不依赖导入顺序（把 import 上下挪一行就能"碰巧好"是脆弱的）。

3. **`NativeDialogs.FolderDialog is not a type`**
   原因：`FolderDialog` 是 Qt 6.3 才加的类型，Qt5 没有。
   处理：`FileDialog { selectFolder: true }`；结果属性在不同实现里落在 `folder` 或 `fileUrl`，取非空那个。

4. **`Cannot assign to non-existent property "onFlagsChanged"`**
   原因：`flagsChanged` 是 Qt6 的 QQuickWindow 才有的信号，Qt5 的 QML Window 没有。
   处理：并入已有的 `onVisibilityChanged`，只在 `visibility === Window.Hidden` 时恢复可见性——最小化是 `Window.Minimized`，不会误触发。（QML 不允许同一属性写两个处理器。）

5. **`QList<double>(size, value)` 编译失败**（`diagnose_check.cpp`）
   原因：(size, value) 构造函数是 Qt6 才加到 `QList` 的。
   处理：显式循环填充。

6. **`connect(..., &QProcess::finished, ...)` 无匹配重载**
   原因：Qt5 的 `finished` 有 `(int)` 与 `(int, ExitStatus)` 两个重载，取地址有歧义；Qt6 删掉了旧重载。
   处理：`QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished)`（3 处）。

7. **`QAudioFormat::setSampleFormat` / `Int16` 不存在**
   原因：Qt5 用 (codec, sampleSize, sampleType) 描述 PCM，Qt6 改成单一 `sampleFormat` 枚举。
   处理：新增 `configurePcmS16()`（`audio/pcm` + 16bit + `SignedInt`），两处调用点统一。

8. **`Threads::Threads` 目标找不到**
   原因：Qt6 的 `find_package` 会间接把 Threads 带进作用域，Qt5 不会；顶层原本没显式声明。
   处理：顶层补 `find_package(Threads REQUIRED)`——本来就该显式，顺手修掉一个隐式依赖。

9. **打包出来只有 41 个二进制、没有 `plugins\` 与 QML 模块（但门禁照样通过）**
   现象：包能过依赖门禁，双击却毫无反应。`dist\...` 下只有 12 个文件，缺 `platforms\qwindows.dll`。
   原因：**`windeployqt` 中途退出了**。它在部署 ANGLE 那一步发现 `libGLESv2.dll` 不存在
   （MSYS2 没装 `angleproject`，本构建也刻意不带 ANGLE，见 D4），报一行错就结束，
   Qt 的几个 DLL 拷完了，`plugins\` 与 QML 模块一个都没拷。
   处理：Qt5 线加 `--no-angle`；并在 `make_release.ps1` 里加**部署完整性断言**
   （`platforms\qwindows.dll`、`Qt5Core.dll`、`QtQuick.2\qmldir`、`QtQuick\Controls.2\qmldir`）。
   教训：门禁只回答"导入都在目标系统上可用吗"，**不回答"东西齐不齐"**——这两个必须分开验。

10. **`qt.conf` 的 `Qml2Imports` 两条线不同**
    原因：Qt6 的 `windeployqt` 把 QML 模块放进 `qml\`，Qt5 的放在**根目录**（`QtQuick.2\`、`QtQml\`…）。
    写错这行，开发机上完全看不出来（读的是 MSYS2 前缀），换台机器就报 `module "QtQuick" is not installed`。
    处理：`make_release.ps1` 按 `$QtMajor` 写 `.` 或 `qml`。

11. **`windeployqt` 需要能解析 exe 的依赖**
    原因：它先扫 exe 的导入再决定部署什么。win81 线的 `avcodec-63.dll` 来自自建共享库
    （不在 MSYS2 的 `bin` 上），解析不到时它只拷 Qt DLL 就收工。
    处理：打包时把 `third_party\win81\ffmpeg-shared\bin` 放进 `PATH`。

**一句话结论**：这批坑全部集中在「Qt6 独有的类型 / 重载 / 构造」上，**没有一处是行为逻辑差异**——播放、导出、诊断的实现原样可用。这也反过来印证了 D1（移植而非重写）的判断。
第 9~11 条不是移植问题，是**部署工具链**的问题：它们的共同特征是「开发机上一切正常」，所以判据必须落在"在一张空目录 + 剥掉 PATH 的机器上还能不能起来"。
