// Read-only adapter for the layouts verified by tools/game_sites.py.
#include <SDL3/SDL.h>
#include <string.h>
#include <stdlib.h>
#include "betop_game_state.h"
#include "betop_haptics.h"
#include "framepace.h"
#include "game_sites.h"
#include "kernel.h"
#include "memscan.h"
#ifdef _WIN32
#include <windows.h>
#endif

typedef struct { uint64_t heap_size, main_end; } BetopReadRange;

static size_t copy_sample(const void *p, size_t n, void *out) {
    memcpy(out, p, n);
    return n;
}

static bool read_sample(void *context, uint64_t address, void *out, size_t size) {
    const BetopReadRange *range = context;
    const uint64_t main_base = 0x7000000000ull;
    bool heap = address >= HEAP_BASE && address - HEAP_BASE <= range->heap_size &&
                size <= range->heap_size - (address - HEAP_BASE);
    bool main = address >= main_base && address <= range->main_end && size <= range->main_end - address;
    if (!size || (!heap && !main)) return false;
#ifdef _WIN32
    // RPM fails on an inaccessible page instead of invoking the guest lazy
    // commit handler for an arbitrary pointer during teardown.
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), (const void *)(uintptr_t)address,
                             out, size, &copied) && copied == size;
#else
    size_t copied = 0;
    return ms_guarded((const void *)(uintptr_t)address, size, copy_sample, out, &copied) && copied == size;
#endif
}

bool betop_game_state_update(SDL_Gamepad *pad, bool allowed) {
    extern volatile int g_movie_playing;
    static unsigned last_flags = ~0u;
    static bool last_valid;
    static uint64_t last_frame, frame_seen_ms;
    unsigned flags = 0;
    bool verified = g_game_version && (!strcmp(g_game_version, "1.8.2") || !strcmp(g_game_version, "1.9.0")) &&
        g_site.betop_state_bow && g_site.betop_state_upper && g_site.betop_state_lower &&
        g_site.betop_state_list && g_site.betop_state_name && g_site.betop_state_guard;
    bool supported = pad && SDL_GetGamepadVendor(pad) == 0x20bc &&
        (SDL_GetGamepadProduct(pad) == 0x5173 || SDL_GetGamepadProduct(pad) == 0x5174);
    bool gameplay = allowed && supported && verified && !g_rt_unwind &&
        g_site.player_info && g_site.pause_state && g_site.screen_mgr && g_site.event_mgr &&
        g_movie_playing == 0;
    uint64_t now = SDL_GetTicks(), frame = framepace_game_frame();
    if (frame != last_frame) {
        last_frame = frame;
        frame_seen_ms = now;
    }
    // A responsive host input loop must not keep force alive indefinitely if
    // the guest frame loop stalls with its last animation still in memory.
    if (!frame || now - frame_seen_ms > 150) gameplay = false;
    if (gameplay) {
        // Includes the pause/map pages and quick menus. No force over cutscenes,
        // loading, the title UI, or an in-game interaction event.
        uint32_t pause = 0;
        BetopReadRange range = {kernel_heap_size(), g_site.main_end};
        gameplay = read_sample(&range, g_site.pause_state, &pause, sizeof pause) && !pause &&
                   !framepace_title_ui() && !framepace_loading_ui() && !framepace_in_event();
    }
    bool valid = false;
    if (gameplay) {
        BetopReadRange range = {kernel_heap_size(), g_site.main_end};
        uint64_t info = 0;
        valid = read_sample(&range, g_site.player_info, &info, sizeof info) &&
                betop_game_state_sample(read_sample, &range, info, &flags);
    }
    if (!valid) flags = 0;
    // No guessed charge timer: until a reliable progress field is mapped,
    // charge uses the scheduler's steady unknown-progress resistance.
    rt_betop_action_state(flags, -1.0f);
    if (getenv("SAVAGEWIND_BETOP_TRACE") && (flags != last_flags || valid != last_valid)) {
        LOG("[betop-state] valid=%d bow=%d guard=%d gameplay=%d\n", valid,
            !!(flags & BETOP_ACTION_BOW_DRAW), !!(flags & BETOP_ACTION_SHIELD_GUARD), gameplay);
    }
    last_flags = flags;
    last_valid = valid;
    return gameplay;
}
