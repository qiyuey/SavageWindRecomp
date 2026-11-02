// Authored KP70C enhancements driven by accepted Player rumble events.
#pragma once
#include <stdbool.h>

typedef struct SDL_Gamepad SDL_Gamepad;
void rt_betop_rumble_event(unsigned type, const char *actor_name);
enum {
    BETOP_ACTION_BOW_DRAW = 1u << 0,
    BETOP_ACTION_SHIELD_GUARD = 1u << 1
};
// Fresh, verified game-action snapshot, not raw button state. Renew within
// 150 ms; flags=0 cancels sustained force. Unknown draw progress is -1;
// progress in [0,1] maps to authored resistance 10..25, otherwise steady 15.
// Calls while disabled/unfocused/disconnected are discarded, not replayed.
void rt_betop_action_state(unsigned flags, float draw_progress);
void betop_haptics_update(SDL_Gamepad *pad, bool allowed);
void betop_haptics_clear(void);
