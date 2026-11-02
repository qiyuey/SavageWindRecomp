// Read-only sampling of verified game action state. No device output.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct SDL_Gamepad SDL_Gamepad;
// Update sustained actions from the current game snapshot; false gates all
// enhancements while unfocused, paused, loading, or outside verified gameplay.
bool betop_game_state_update(SDL_Gamepad *pad, bool allowed);

typedef bool (*BetopReadMemory)(void *context, uint64_t address, void *out, size_t size);

// player_info is the PlayerInfo object, not the global that points to it.
// On any invalid/incomplete chain returns false and writes flags=0. A valid
// non-bow snapshot returns true with flags=0. The caller supplies a bounded
// reader and enforces gameplay/pause/focus gates before applying a snapshot.
bool betop_game_state_sample(BetopReadMemory read_memory, void *context,
                             uint64_t player_info, unsigned *flags);
