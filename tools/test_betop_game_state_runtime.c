// Adapter gates and scheduler integration with synthetic reads only.
#include <assert.h>
#include <stdio.h>
#include <windows.h>
static BOOL WINAPI fake_read(HANDLE process, LPCVOID address, LPVOID out, SIZE_T size, SIZE_T *copied);
#define ReadProcessMemory fake_read
#include "../runtime/betop_game_state_runtime.c"
#undef ReadProcessMemory
#include "../runtime/betop_haptics.c"

GameSites g_site;
const char *g_game_version;
volatile int g_movie_playing, g_rt_unwind;
static bool title_ui, loading_ui, in_event, valid_sample, failed_read, short_read;
static uint32_t pause_value;
static unsigned sampled_flags, sample_calls, read_calls;
static Uint64 clock_ms;
static uint64_t game_frame = 1;
static bool freeze_frame;
static unsigned output_calls;
static SDL_BetopGamepadEffects output;
static SDL_Gamepad *const kp70 = (SDL_Gamepad *)(uintptr_t)1;
static SDL_Gamepad *const other = (SDL_Gamepad *)(uintptr_t)2;

Uint64 SDL_GetTicks(void) { return clock_ms; }
Uint16 SDL_GetGamepadVendor(SDL_Gamepad *pad) { return pad == kp70 ? 0x20bc : 0x045e; }
Uint16 SDL_GetGamepadProduct(SDL_Gamepad *pad) { return pad == kp70 ? 0x5174 : 0x02e0; }
const char *SDL_GetError(void) { return "synthetic failure"; }
bool SDL_SetGamepadBetopEffects(SDL_Gamepad *pad, const SDL_BetopGamepadEffects *effects, Uint32 lease) {
    assert(pad == kp70);
    assert(!has_effects(effects) || (lease && lease <= 100));
    output = *effects;
    ++output_calls;
    return true;
}
uint64_t kernel_heap_size(void) { return 0x10000; }
uint64_t framepace_game_frame(void) { return game_frame; }
bool framepace_title_ui(void) { return title_ui; }
bool framepace_loading_ui(void) { return loading_ui; }
bool framepace_in_event(void) { return in_event; }
bool betop_game_state_sample(BetopReadMemory read_memory, void *context, uint64_t info, unsigned *flags) {
    assert(read_memory && context && info == HEAP_BASE + 0x100);
    ++sample_calls;
    *flags = sampled_flags;
    return valid_sample;
}
static BOOL WINAPI fake_read(HANDLE process, LPCVOID address, LPVOID out, SIZE_T size, SIZE_T *copied) {
    (void)process;
    ++read_calls;
    *copied = 0;
    if (failed_read) return FALSE;
    uint64_t p = (uint64_t)(uintptr_t)address;
    if (p == g_site.pause_state && size == sizeof pause_value) {
        memcpy(out, &pause_value, size);
    } else if (p == g_site.player_info && size == sizeof(uint64_t)) {
        uint64_t info = HEAP_BASE + 0x100;
        memcpy(out, &info, size);
    } else return FALSE;
    *copied = short_read ? size - 1 : size;
    return TRUE;
}

static bool update(SDL_Gamepad *pad, bool allowed) {
    clock_ms += 25;
    if (!freeze_frame) ++game_frame;
    bool gameplay = betop_game_state_update(pad, allowed);
    betop_haptics_update(pad, gameplay);
    return gameplay;
}
static void start_action(void) {
    update(kp70, true); // First update after disabled state establishes acceptance.
    assert(update(kp70, true));
    assert(output.triggers[1].mode == SDL_BETOP_TRIGGER_RESISTANCE);
}
static void expect_gate(void) {
    unsigned before = sample_calls;
    assert(!update(kp70, true));
    assert(!has_effects(&output) && sample_calls == before);
}
int main(void) {
    unsetenv("SAVAGEWIND_BETOP");
    unsetenv("SAVAGEWIND_BETOP_TRACE");
    g_game_version = "1.8.2";
    g_site.main_end = MAIN_BASE + 0x10000;
    g_site.player_info = MAIN_BASE + 0x100;
    g_site.pause_state = MAIN_BASE + 0x200;
    g_site.screen_mgr = g_site.event_mgr = MAIN_BASE + 0x300;
    g_site.betop_state_bow = g_site.betop_state_upper = g_site.betop_state_lower = 1;
    g_site.betop_state_list = g_site.betop_state_name = g_site.betop_state_guard = 1;
    sampled_flags = BETOP_ACTION_BOW_DRAW;
    valid_sample = true;
    start_action();
    assert(output.triggers[1].parameters[1] == 15);
    assert(!update(kp70, false) && !has_effects(&output));
    start_action();
    pause_value = 2; expect_gate(); pause_value = 0;
    start_action(); title_ui = true; expect_gate(); title_ui = false;
    start_action(); loading_ui = true; expect_gate(); loading_ui = false;
    start_action(); in_event = true; expect_gate(); in_event = false;
    start_action(); g_movie_playing = 1; expect_gate(); g_movie_playing = 0;
    start_action(); g_rt_unwind = 1; expect_gate(); g_rt_unwind = 0;
    start_action(); freeze_frame = true;
    clock_ms += 151; expect_gate(); freeze_frame = false;
    start_action(); freeze_frame = true; game_frame = 0;
    expect_gate(); freeze_frame = false;
    start_action(); g_game_version = "future"; expect_gate(); g_game_version = "1.9.0";
    start_action(); g_site.betop_state_guard = 0; expect_gate(); g_site.betop_state_guard = 1;
    start_action(); failed_read = true; expect_gate(); failed_read = false;
    start_action(); short_read = true; expect_gate(); short_read = false;
    start_action();
    valid_sample = false;
    assert(update(kp70, true)); // Contact policy may remain active, sustained force cannot.
    assert(!has_effects(&output));
    valid_sample = true;
    start_action();
    setenv("SAVAGEWIND_BETOP", "0", 1);
    update(kp70, true); assert(!has_effects(&output));
    update(kp70, true); assert(!has_effects(&output));
    unsetenv("SAVAGEWIND_BETOP");
    sampled_flags = 0;
    update(kp70, true); update(kp70, true); assert(!has_effects(&output));
    sampled_flags = BETOP_ACTION_BOW_DRAW;
    start_action();
    assert(!update(NULL, false));
    sampled_flags = 0;
    update(kp70, true); update(kp70, true);
    assert(!action_flags); // Closing/disconnect stops device via input/SDL layer.
    unsigned before = read_calls;
    assert(!update(other, true) && read_calls == before);

    // Bounded reader rejects addresses before calling the platform API.
    BetopReadRange range = {0x10000, MAIN_BASE + 0x10000};
    uint64_t value;
    before = read_calls;
    assert(!read_sample(&range, 1, &value, sizeof value));
    assert(!read_sample(&range, MAIN_BASE + 0x10000, &value, sizeof value));
    assert(!read_sample(&range, HEAP_BASE + 0xffff, &value, sizeof value));
    assert(!read_sample(&range, UINT64_MAX, &value, sizeof value));
    assert(read_calls == before);
    assert(output_calls > 0);
    puts("betop runtime: valid snapshots, focus/pause/event/loading/title/movie/unwind/version gates, failed reads, disable and range checks passed");
    return 0;
}
