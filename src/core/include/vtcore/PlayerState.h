#pragma once

namespace vtcore {

enum class PlayerStatus {
    Idle,        // 未加载媒体
    Loading,     // 正在打开/解封装
    Playing,
    Paused,
    Eof,         // 播放到结尾
};

const char* toString(PlayerStatus s);

} // namespace vtcore