# media-pipeline

## ADDED Requirements

### Requirement: 打开主流音视频文件
系统 SHALL 通过 libavformat 打开本地音视频文件并完成流探测，支持主流容器与编码（MP4/H.264、MKV/H.264/H.265、MOV、AVI、WebM/VP9、MP3、FLAC、WAV、AAC、OGG）。

#### Scenario: 打开 MP4 视频
- **WHEN** 用户打开一个 H.264 编码的 MP4 文件
- **THEN** 管线完成流探测，报告时长、视频尺寸、音视频流信息，并开始输出解码帧

#### Scenario: 打开纯音频文件
- **WHEN** 用户打开一个 MP3 或 FLAC 文件
- **THEN** 管线仅输出音频流，视频渲染区显示品牌占位画面

#### Scenario: 打开损坏或不支持的文件
- **WHEN** 用户打开损坏的文件或管线探测失败的文件
- **THEN** 管线返回明确错误并回到 Idle 状态，应用不崩溃

### Requirement: 软件解码输出
系统 SHALL 使用 libavcodec 软件解码视频与音频流，视频帧经 libswscale 转换为可上传纹理的格式（RGBA），音频经 libswresample 统一重采样为固定格式（S16 / 立体声 / 48kHz）。

#### Scenario: 解码主流分辨率视频
- **WHEN** 播放 1080p H.264 视频
- **THEN** 解码帧率稳定达到视频原始帧率，无持续丢帧

### Requirement: 音视频同步（音频主时钟）
系统 SHALL 以音频输出已播放位置为主时钟进行音视频同步：视频帧 PTS 早于主时钟则等待显示，晚于主时钟则丢弃追赶。

#### Scenario: 正常播放同步
- **WHEN** 播放包含音视频流的文件 60 秒
- **THEN** 音画偏差始终在 ±50ms 以内，无累计漂移

#### Scenario: 视频解码短暂落后
- **WHEN** 某视频帧解码完成时其 PTS 已落后主时钟超过一帧
- **THEN** 该帧被跳过，播放继续且音频不中断

### Requirement: 精准 seek
系统 SHALL 提供帧级精准 seek：先定位目标时间之前最近的关键帧，前向解码至目标 PTS 后恢复输出，误差不超过 1 帧。

#### Scenario: 进度条拖拽跳转
- **WHEN** 用户将进度条拖到任意位置松开
- **THEN** 画面在 1 秒内显示目标时间对应的帧（误差 ≤1 帧），音频同步从该位置继续

#### Scenario: 纯音频文件 seek
- **WHEN** 对纯音频文件执行 seek
- **THEN** 音频立即从目标位置继续播放

### Requirement: 逐帧步进
系统 SHALL 支持暂停状态下逐帧前进（前向解码到下一帧 PTS）。

#### Scenario: 暂停后逐帧
- **WHEN** 播放暂停时用户按下逐帧快捷键
- **THEN** 画面前进且仅前进一帧，保持暂停状态

### Requirement: 倍速播放
系统 SHALL 支持 0.5x–2x 倍速播放，音频与视频同时变速，状态切换无中断。

#### Scenario: 切换倍速
- **WHEN** 播放中用户将倍速从 1.0x 切换到 2.0x
- **THEN** 音视频立即以 2 倍速同步播放，无需重新加载文件

### Requirement: 音频帧回调接口（AI 预留）
系统 SHALL 暴露音频帧观察者接口（PCM 数据 + PTS 回调），供未来 AI 字幕模块订阅，且 MVP 阶段该接口的存在不影响性能。

#### Scenario: 注册音频观察者
- **WHEN** 代码向管线注册 AudioFrameObserver
- **THEN** 播放过程中观察者按帧收到 PCM 数据与对应 PTS
