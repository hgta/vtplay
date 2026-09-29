#include "PlayerState.h"

namespace vtcore {

const char* toString(PlayerStatus s) {
    switch (s) {
        case PlayerStatus::Idle:    return "Idle";
        case PlayerStatus::Loading: return "Loading";
        case PlayerStatus::Playing: return "Playing";
        case PlayerStatus::Paused:  return "Paused";
        case PlayerStatus::Eof:     return "Eof";
    }
    return "?";
}

} // namespace vtcore