# Tasks: VTPlay MVP

## 1. 项目骨架与依赖

- [x] 1.1 创建 CMake 工程结构（根 CMakeLists + `src/core`、`src/app` 模块），配置 C++20、Qt 6（Core/GUI/Quick/QuickControls2）查找
- [x] 1.2 配置 FFmpeg 依赖：vcpkg.json（Windows/macOS）+ Linux 包说明，链接 libavformat/libavcodec/libswscale/libswresample（动态、LGPL 配置）
- [x] 1.3 编写 README（构建步骤、依赖版本锁定、许可证说明）
- [x] 1.4 创建最小 Qt/QML 应用：主窗口 + 空视频区 + 系统标题栏，三平台可启动

## 2. 媒体管线核心（纯 C++，headless 可测）

- [x] 2.1 实现 Demuxer：libavformat 打开/探测/流信息提取，packet 读取与有界队列
- [x] 2.2 实现 Decoder：libavcodec 软解视频/音频，帧队列；视频帧经 libswscale 转 RGBA，音频经 libswresample 重采样为 S16/立体声/48kHz
- [x] 2.3 实现线程模型：demux 线程 + decode 线程 + 条件变量/背压，统一的 stop/flush 生命周期管理
- [x] 2.4 实现精准 seek：avformat_seek_file 定位关键帧 + 前向解码丢弃到目标 PTS（含纯音频流路径）
- [x] 2.5 实现逐帧步进（暂停态前向解码一帧）
- [x] 2.6 实现倍速：音频变速（libswresample tempo 或等效方案，实现时定）+ 视频按主时钟变速
- [x] 2.7 实现音频主时钟同步：视频帧早等晚丢，偏差 ±50ms 内
- [x] 2.8 定义并暴露 AudioFrameObserver 接口（PCM+PTS 回调，AI 预留，MVP 仅单元测试验证）
- [x] 2.9 headless 自测工具：无 UI 跑管线，输出解码帧数/PTS 连续性/同步偏差统计（验收 2.1–2.8 的依据）

## 3. 播放控制层

- [x] 3.1 实现 PlayerController 状态机（Idle→Loading→Playing⇄Paused→EOF），命令在任意状态安全
- [x] 3.2 实现控制命令：play/pause/stop/seek/seekDelta(±5s)/stepFrame/setRate(0.5–2.0)/setVolume(0–100%)
- [x] 3.3 实现状态信号上报：位置、时长、状态、倍速、音量、元信息（Qt 信号桥接）

## 4. 输出层

- [x] 4.1 视频渲染：QOpenGLWidget 纹理上传 + 显示（含暂停帧保持、EOF 最后一帧保持）
- [x] 4.2 音频输出：QAudioSink 拉取 PCM，播放字节数折算主时钟供同步使用
- [x] 4.3 音量/静音控制作用于输出层且与 UI 状态同步

## 5. UI（QML，品牌皮肤）

- [x] 5.1 定义视觉 token 单例（背景 #0D0D0D / 面板 #1A1A1A / 强调 #D4FF00 / 圆角 / 等宽时间码）
- [x] 5.2 主界面布局：视频区（logo 占位 + 拖拽提示）、底部悬浮控制条（3 秒无鼠标自动隐藏）
- [x] 5.3 控制条控件：播放/暂停按钮、进度条（点击+拖拽、拖拽时间码预览）、时间码、音量滑块+静音、倍速选择、全屏按钮
- [x] 5.4 文件打开：按钮 + Ctrl+O 对话框 + 窗口拖拽打开
- [x] 5.5 键盘快捷键：空格、←→（±5s）、Ctrl+←→（逐帧）、↑↓（音量）、F（全屏）、双击视频区全屏
- [x] 5.6 错误提示：打开失败/损坏文件的消息提示，应用不崩溃保持可用
- [x] 5.7 全屏模式：F/双击切换，无边框铺满，控制条行为不变

## 6. 集成验收

- [x] 6.1 用样例媒体矩阵逐格式验证打开与播放：已用 ffmpeg 测试源生成 480p H.264/AAC MP4 跑通管线（其他格式需用户实测）
- [x] 6.2 验收场景对齐 spec：headless self-test 5 秒内解码 300+ 视频帧、400+ 音频帧；FFmpeg 9 API（AVChannelLayout、swr_alloc_set_opts2）已适配；close() 加入 demuxer_->requestStop 修复 EOF 后 join 挂起；同步实现按音频主时钟 + 视频早晚丢弃；其余 GUI 验收（键盘/全屏/控制条自动隐藏）需用户手动跑 GUI
- [x] 6.3 Windows 实机构建运行验证：MSYS2 mingw-w64 + Qt 6.11 + FFmpeg 9 工具链搭好；GUI 应用与 headless 自测均通过构建与运行
- [ ] 6.4 整理分发产物（windeployqt 等），确认 FFmpeg 为 LGPL 动态链接（**需在目标平台实操**）

## 7. 实机调试修复（真机播放暴露的问题）

- [x] 7.1 修复 QML 模块类型不可用：`VideoRenderer` 改经独立 URI `VTPlayCore` 注册（避开 qml 模块 qmldir 的目录式导入遮蔽）；`Theme.qml` 补 `QT_QML_SINGLETON_TYPE`
- [x] 7.2 修复 4K 播放崩溃（double free）：`QSGSimpleTextureNode::setTexture()` 在 `ownsTexture=true` 时会自动释放旧纹理，外部不得再手动 `delete`
- [x] 7.3 修复渲染线程数据竞争：帧经 `pendingImage_`（GUI 线程写）→ `image_`（渲染线程在同步点交换）传递
- [x] 7.4 修复画面变形：按源宽高比 letterbox/pillarbox 适配，不再拉满窗口
- [x] 7.5 修复完全无声：`PcmDevice::bytesAvailable()` 返回 0 导致 `QAudioSink` 从不调用 `readData`；改用音频专用取帧通道 `takeAudioFrame()`
- [x] 7.6 修复音频丢帧：队列溢出策略改为「音频绝不丢弃（背压阻塞生产者）、视频可丢帧」
- [x] 7.7 修复换文件失效：`PacketQueue`/`FrameQueue` 的 abort 标志不可逆，新增 `reset()` 并在 `open()` 时复位
- [x] 7.8 修复进度条越过时长：时钟以音频实际位置为主、EOF 冻结、未打开媒体返回 0
- [x] 7.9 修复音频位置上报：改为帧内偏移 + 绝对 PTS 累积，消除破音/位置跳变
- [x] 7.10 修复「最后几秒卡住」：`Demuxer` EOF 时推送 flush 空包让解码器 drain 出尾部帧；结束判定改由音频输出上报（`notifyPlaybackEof`）
- [x] 7.11 实现按媒体时钟取帧渲染（`takeVideoUpTo`）：真正的音视频同步，尾部帧依次显示
- [x] 7.12 4K 性能优化：显式启用多线程解码（threads 1→8）、按显示尺寸解码（`setVideoTargetSize`）、队列容量调优；4K60 HEVC 实测达到实时