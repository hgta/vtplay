# playback-control

## ADDED Requirements

### Requirement: 播放状态机
系统 SHALL 实现并对外暴露播放状态机：Idle → Loading → Playing ⇄ Paused → EOF，所有控制命令在任意状态下均为安全操作（非法命令被忽略而非报错）。

#### Scenario: 正常状态流转
- **WHEN** 用户打开文件并点击播放、暂停、再播放
- **THEN** 状态依次经过 Loading → Playing → Paused → Playing，UI 实时反映状态

#### Scenario: 播放到结尾
- **WHEN** 文件播放至末尾
- **THEN** 状态进入 EOF，画面停留在最后一帧

### Requirement: 基础播放控制命令
系统 SHALL 提供统一控制接口：play、pause、stop、seek(时间)、seekDelta(±秒)、stepFrame、setRate(0.5–2.0)、setVolume(0–100%)。

#### Scenario: 停止后重新播放
- **WHEN** 用户在 Paused 状态点击停止
- **THEN** 管线释放当前媒体，状态回到 Idle，可打开新文件

### Requirement: 快捷键操作
系统 SHALL 支持键盘快捷操作：空格=播放/暂停，←→=±5s，Ctrl+←→=逐帧，↑↓=音量，F=全屏，双击视频区=全屏切换。

#### Scenario: 键盘跳转
- **WHEN** 播放中用户按 → 键
- **THEN** 播放位置前进 5 秒（帧级精准），无需鼠标操作

### Requirement: 状态信号上报
系统 SHALL 向 UI 层上报状态变化信号：当前播放位置、时长、播放/暂停状态、倍速、音量、文件元信息。

#### Scenario: UI 进度刷新
- **WHEN** 播放进行中
- **THEN** UI 每帧获得位置更新，进度条平滑反映当前位置
