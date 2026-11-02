// The SDK mixes its vibration nodes itself, then sends HID commands 201 / 206.
// Preserve both bands on each side through the custom SDL3 HD-rumble driver.
#include <SDL3/SDL.h>
#include <math.h>
#include <pthread.h>
#include <string.h>

#include "../hle.h"
#include "../vibration.h"
#include "../settings.h"
#include "../betop_haptics.h"
#include "../betop_game_state.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static VibrationValue values[2] = {{0, 160, 0, 320}, {0, 160, 0, 320}};
static bool permitted = true;

// These are the handles returned by hid_vibration_handles (input.c), for our one player or handheld mode.
static int device(uint32_t handle) {
    uint32_t id = (handle >> 8) & 0xff;
    return (handle & 0xfffe00ffu) == 3 && (id == 0 || id == 0x20) ? (int)((handle >> 16) & 1) : -1;
}

static float amplitude(float a) { return !isfinite(a) || a < 0 ? 0 : a > 1 ? 1 : a; }

void vibration_send(uint32_t handle, const VibrationValue *value) {
    int i = device(handle);
    if (i < 0) return;
    pthread_mutex_lock(&lock);
    if (permitted && g_settings.rumble) {
        VibrationValue previous = values[i];
        values[i] = *value;
        values[i].low_amplitude = amplitude(value->low_amplitude);
        values[i].high_amplitude = amplitude(value->high_amplitude);
        if (!isfinite(value->low_frequency) || !isfinite(value->high_frequency))
            values[i] = (VibrationValue){0, 160, 0, 320};
        if ((previous.low_amplitude != values[i].low_amplitude || previous.high_amplitude != values[i].high_amplitude ||
             previous.low_frequency != values[i].low_frequency || previous.high_frequency != values[i].high_frequency) &&
            getenv("SAVAGEWIND_VIBRATION"))
            LOG("[vibration] ms %llu handle %#x low %.6f@%.3fHz high %.6f@%.3fHz\n",
                (unsigned long long)SDL_GetTicks(), handle,
                values[i].low_amplitude, values[i].low_frequency,
                values[i].high_amplitude, values[i].high_frequency);
    }
    pthread_mutex_unlock(&lock);
}

VibrationValue vibration_get(uint32_t handle) {
    VibrationValue v = {0, 160, 0, 320};
    int i = device(handle);
    pthread_mutex_lock(&lock);
    if (i >= 0 && permitted && g_settings.rumble) v = values[i];
    pthread_mutex_unlock(&lock);
    return v;
}

void vibration_clear(void) {
    pthread_mutex_lock(&lock);
    for (int i = 0; i < 2; i++) values[i] = (VibrationValue){0, 160, 0, 320};
    pthread_mutex_unlock(&lock);
    betop_haptics_clear();
}

void vibration_permit(bool allow) {
    pthread_mutex_lock(&lock);
    permitted = allow;
    if (!allow) memset(values, 0, sizeof values);
    pthread_mutex_unlock(&lock);
    if (!allow) betop_haptics_clear();
}

bool vibration_permitted(void) {
    pthread_mutex_lock(&lock);
    bool allow = permitted;
    pthread_mutex_unlock(&lock);
    return allow;
}

RT_ON_RESET(vibration_reset) {
    vibration_clear();
    vibration_permit(true);
}

void vibration_update(SDL_Gamepad *pad, bool focused) {
    // Default USB Switch 2 frequency path on; an explicit environment opt-out
    // remains possible. User gain is owned here, never applied a second time in SDL.
    static bool configured;
    if (!configured) {
        SDL_SetHintWithPriority("SDL_JOYSTICK_HIDAPI_SWITCH2_EXPERIMENTAL_HD", "1", SDL_HINT_DEFAULT);
        SDL_SetHintWithPriority("SDL_JOYSTICK_HIDAPI_SWITCH2_HD_GAIN", "1", SDL_HINT_OVERRIDE);
        configured = true;
    }
    static SDL_Gamepad *last_pad;
    static VibrationValue last[2];
    static uint64_t last_ms;
    static bool failed;
    if (!pad || !focused || !g_settings.rumble) vibration_clear();
    // Enhanced semantic pulses have their own deadlines, even if grip values
    // are unchanged. Always service them before the ordinary-rumble fast path.
    bool enhanced_allowed = betop_game_state_update(pad, focused && g_settings.rumble && g_settings.rumble_strength > 0 && vibration_permitted());
    betop_haptics_update(pad, enhanced_allowed);
    bool changed = pad != last_pad;
    if (changed) {
        last_pad = pad;
        memset(last, 0, sizeof last);
        last_ms = 0;
        failed = false;
    }
    if (!pad) return;
    pthread_mutex_lock(&lock);
    VibrationValue current[2] = {{0, 160, 0, 320}, {0, 160, 0, 320}};
    if (focused && permitted && g_settings.rumble) memcpy(current, values, sizeof current);
    pthread_mutex_unlock(&lock);
    for (int i = 0; i < 2; ++i)
        rumble_apply_strength(&current[i].low_amplitude, &current[i].high_amplitude, g_settings.rumble_strength);
    bool active = current[0].low_amplitude || current[0].high_amplitude ||
                  current[1].low_amplitude || current[1].high_amplitude;
    bool was_active = last[0].low_amplitude || last[0].high_amplitude ||
                      last[1].low_amplitude || last[1].high_amplitude;
    uint64_t now = SDL_GetTicks();
    // A short lease stops the motors if the event loop stalls; refresh unchanged effects while it is alive.
    if (!changed && memcmp(current, last, sizeof current) == 0 && (!active || now - last_ms < 50)) return;
    if (!SDL_RumbleGamepadFrequenciesEx(pad,
            current[0].low_amplitude, current[0].low_frequency,
            current[0].high_amplitude, current[0].high_frequency,
            current[1].low_amplitude, current[1].low_frequency,
            current[1].high_amplitude, current[1].high_frequency, active ? 100 : 0) && !failed) {
        LOG("[input] controller rumble unavailable: %s\n", SDL_GetError());
        failed = true;
    }
    static int nlog;  // (SAVAGEWIND_MOTION_LOG: rumble going on and off, next to the sensor stream's health - a
                      // controller might change its sensor reports once it rumbles, #220)
    if (active != was_active && ENV("SAVAGEWIND_MOTION_LOG") && nlog < 200) {
        nlog++;
        LOG("[motion] rumble %s\n", active ? "on" : "off");
    }
    memcpy(last, current, sizeof last);
    last_ms = now;
}
