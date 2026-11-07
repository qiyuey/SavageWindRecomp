// Preserve the SDK's mixed low/high bands and stereo state through SDL.
#include <SDL3/SDL.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#undef IN
#undef OUT
#endif
#include "../hle.h"
#include "../vibration.h"
#include "../settings.h"
#include "../betop_haptics.h"
#include "../betop_game_state.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t output_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static pthread_t worker;
#ifdef _WIN32
static HANDLE wake_event;
#endif
static void notify_locked(void) {
    pthread_cond_signal(&wake);
#ifdef _WIN32
    if (wake_event) SetEvent(wake_event);
#endif
}
static VibrationValue values[2] = {{0, 160, 0, 320}, {0, 160, 0, 320}}, last[2];
static bool permitted = true, accepting = true, focused_snapshot;
static bool running, started, have_last, retry, stop_pending;
static SDL_Gamepad *output_pad;
static int strength_snapshot;
static uint64_t heartbeat_ns, submitted_ns, retry_after_ns, sequence, received_ns, pumped_ns, stop_generation;

static int device(uint32_t handle) {
    uint32_t id = (handle >> 8) & 0xff;
    return (handle & 0xfffe00ffu) == 3 && (id == 0 || id == 0x20) ? (int)((handle >> 16) & 1) : -1;
}
static float amplitude(float a) { return !isfinite(a) || a < 0 ? 0 : a > 1 ? 1 : a; }

static void neutral_locked(void) {
    for (int i = 0; i < 2; ++i) values[i] = (VibrationValue){0, 160, 0, 320};
    if (!stop_pending) retry_after_ns = 0;
    stop_pending = true;
    ++stop_generation;
    ++sequence;
    received_ns = SDL_GetTicksNS();
    notify_locked();
}
static void store_locked(uint32_t handle, const VibrationValue *value) {
    int i = device(handle);
    if (i < 0 || !permitted || !accepting) return;
    VibrationValue previous = values[i];
    values[i] = *value;
    values[i].low_amplitude = amplitude(value->low_amplitude);
    values[i].high_amplitude = amplitude(value->high_amplitude);
    if (!isfinite(value->low_frequency) || !isfinite(value->high_frequency))
        values[i] = (VibrationValue){0, 160, 0, 320};
    if (memcmp(&previous, &values[i], sizeof previous) && getenv("SAVAGEWIND_VIBRATION"))
        LOG("[vibration] ms %llu handle %#x low %.6f@%.3fHz high %.6f@%.3fHz\n",
            (unsigned long long)SDL_GetTicks(), handle,
            values[i].low_amplitude, values[i].low_frequency,
            values[i].high_amplitude, values[i].high_frequency);
    ++sequence;
    received_ns = SDL_GetTicksNS();
}
static void publish_locked(void) {
    // A complete silence must survive a subsequent nonzero update before the
    // worker wakes. Active changes otherwise replace the latest state in place.
    if (!values[0].low_amplitude && !values[0].high_amplitude &&
        !values[1].low_amplitude && !values[1].high_amplitude) {
        stop_pending = true;
        ++stop_generation;
    }
    notify_locked();
}
void vibration_send(uint32_t handle, const VibrationValue *value) {
    pthread_mutex_lock(&lock);
    store_locked(handle, value);
    publish_locked();
    pthread_mutex_unlock(&lock);
}
void vibration_send_batch(const void *handles, const void *data, uint64_t count) {
    pthread_mutex_lock(&lock);
    for (uint64_t i = 0; i < count; ++i) {
        uint32_t handle;
        VibrationValue value;
        memcpy(&handle, (const char *)handles + i * sizeof handle, sizeof handle);
        memcpy(&value, (const char *)data + i * sizeof value, sizeof value);
        store_locked(handle, &value);
    }
    publish_locked();
    pthread_mutex_unlock(&lock);
}
VibrationValue vibration_get(uint32_t handle) {
    VibrationValue v = {0, 160, 0, 320};
    int i = device(handle);
    pthread_mutex_lock(&lock);
    if (i >= 0 && permitted && accepting) v = values[i];
    pthread_mutex_unlock(&lock);
    return v;
}
void vibration_clear(void) {
    pthread_mutex_lock(&lock);
    neutral_locked();
    pthread_mutex_unlock(&lock);
    betop_haptics_clear();
}
void vibration_permit(bool allow) {
    pthread_mutex_lock(&lock);
    permitted = allow;
    if (!allow) neutral_locked();
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

// Output and detach share output_lock, acquired before lock. Release the
// state lock around SDL: game submissions never wait on device I/O or its locks.
// Settings and guest-memory sampling remain on the event thread.
static void output_locked(uint64_t now, bool manual) {
    if (!output_pad) return;
    if (!manual && focused_snapshot && now - heartbeat_ns >= 100000000) {
        accepting = focused_snapshot = false;
        neutral_locked();
    }
    VibrationValue current[2] = {{0, 160, 0, 320}, {0, 160, 0, 320}};
    if (!stop_pending && focused_snapshot && permitted && accepting)
        memcpy(current, values, sizeof current);
    for (int i = 0; i < 2; ++i)
        rumble_apply_strength(&current[i].low_amplitude, &current[i].high_amplitude, strength_snapshot);
    bool active = current[0].low_amplitude || current[0].high_amplitude ||
                  current[1].low_amplitude || current[1].high_amplitude;
    bool changed = !have_last || memcmp(current, last, sizeof current);
    if (!changed && !retry && (!active || now - submitted_ns < 50000000)) {
        stop_pending = false;
        return;
    }
    // New values are event-driven. Only a failed output backs off.
    if (!manual && retry && now < retry_after_ns) return;
    retry_after_ns = 0;
    uint64_t sent_sequence = sequence, sent_received_ns = received_ns;
    uint64_t sent_stop_generation = stop_generation;
    SDL_Gamepad *sending_pad = output_pad;
    pthread_mutex_unlock(&lock);
    bool ok = SDL_RumbleGamepadFrequenciesEx(sending_pad,
        current[0].low_amplitude, current[0].low_frequency,
        current[0].high_amplitude, current[0].high_frequency,
        current[1].low_amplitude, current[1].low_frequency,
        current[1].high_amplitude, current[1].high_frequency, active ? 100 : 0);
    if (ok) SDL_UpdateJoysticks();
    pthread_mutex_lock(&lock);
    if (getenv("SAVAGEWIND_VIBRATION_TIMING"))
        LOG("[vibration-send] ns %llu seq %llu age_us %llu active %d accepted %d\n",
            (unsigned long long)now, (unsigned long long)sent_sequence,
            (unsigned long long)(now >= sent_received_ns ? (now - sent_received_ns) / 1000 : 0), active, ok);
    if (!ok) {
        if (!retry) LOG("[input] controller rumble unavailable: %s\n", SDL_GetError());
        retry = true;
        retry_after_ns = SDL_GetTicksNS() + 4000000;
        return;
    }
    // Switch 2 nonzero requests enter its queue during the device update above.
    pumped_ns = SDL_GetTicksNS();
    bool was_active = have_last && (last[0].low_amplitude || last[0].high_amplitude ||
                                    last[1].low_amplitude || last[1].high_amplitude);
    static unsigned nlog;
    if (active != was_active && getenv("SAVAGEWIND_MOTION_LOG") && nlog++ < 200)
        LOG("[motion] rumble %s\n", active ? "on" : "off");
    retry = false;
    if (stop_generation == sent_stop_generation) stop_pending = false;
    have_last = true;
    memcpy(last, current, sizeof last);
    submitted_ns = now;
}
static void *vibration_worker(void *unused) {
    (void)unused;
#ifdef _WIN32
    // winpthreads timed waits can round a 4 ms timeout up to a 15.6 ms tick.
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, 0x2, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(NULL, FALSE, NULL);
#endif
    pthread_mutex_lock(&lock);
    while (running) {
        pthread_mutex_unlock(&lock);
        pthread_mutex_lock(&output_lock);
        pthread_mutex_lock(&lock);
        if (!running) { pthread_mutex_unlock(&output_lock); break; }
        output_locked(SDL_GetTicksNS(), false);
        uint64_t poll_now = SDL_GetTicksNS();
        if (output_pad && poll_now - pumped_ns >= 4000000) {
            pthread_mutex_unlock(&lock);
            SDL_UpdateJoysticks();
            pthread_mutex_lock(&lock);
            pumped_ns = SDL_GetTicksNS();
        }
        // Fresh publications wake output immediately. Timed waits only service
        // input, leases and failed-output retries.
        bool active = have_last && (last[0].low_amplitude || last[0].high_amplitude ||
                                    last[1].low_amplitude || last[1].high_amplitude);
        bool requested = focused_snapshot && permitted && accepting && strength_snapshot > 0 &&
                         (values[0].low_amplitude || values[0].high_amplitude ||
                          values[1].low_amplitude || values[1].high_amplitude);
        bool idle = !output_pad || (!active && !requested && !retry && !stop_pending);
        uint64_t now = SDL_GetTicksNS();
        uint64_t poll_due = pumped_ns + 4000000;
        uint64_t delay = poll_due > now ? poll_due - now : 4000000;
        if (retry && retry_after_ns > now && retry_after_ns - now < delay)
            delay = retry_after_ns - now;
#ifdef _WIN32
        if (!timer) {
            LOG("[input] vibration timer creation failed\n");
            accepting = focused_snapshot = false;
            neutral_locked();
            output_locked(now, true);
            running = false;
            pthread_mutex_unlock(&output_lock);
            break;
        }
        if (!idle) {
            LARGE_INTEGER due;
            // This timeout only services input/leases. New output wakes the event.
            due.QuadPart = -(LONGLONG)((delay + 99) / 100);
            if (!SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) {
                accepting = focused_snapshot = false;
                neutral_locked();
                output_locked(now, true);
                LOG("[input] vibration timer arm failed\n");
                running = false;
                pthread_mutex_unlock(&output_lock);
                break;
            }
        }
        HANDLE waits[] = {wake_event, timer};
        pthread_mutex_unlock(&output_lock);
        pthread_mutex_unlock(&lock);
        DWORD result = WaitForMultipleObjects(idle ? 1 : 2, waits, FALSE, INFINITE);
        pthread_mutex_lock(&lock);
        if (result == WAIT_FAILED) {
            accepting = focused_snapshot = false;
            neutral_locked();
            LOG("[input] vibration timer wait failed\n");
            running = false;
        }
#else
        pthread_mutex_unlock(&output_lock);
        if (idle) pthread_cond_wait(&wake, &lock);
        else {
            struct timespec until;
            timespec_get(&until, TIME_UTC);
            until.tv_nsec += (long)delay;
            if (until.tv_nsec >= 1000000000) { ++until.tv_sec; until.tv_nsec -= 1000000000; }
            pthread_cond_timedwait(&wake, &lock, &until);
        }
#endif
    }
#ifdef _WIN32
    if (timer) CloseHandle(timer);
#endif
    pthread_mutex_unlock(&lock);
    return NULL;
}
void vibration_detach(SDL_Gamepad *pad) {
    pthread_mutex_lock(&output_lock);
    pthread_mutex_lock(&lock);
    if (pad == output_pad) {
        accepting = focused_snapshot = false;
        neutral_locked();
        output_locked(SDL_GetTicksNS(), true);
        output_pad = NULL;
        have_last = retry = false;
    }
    pthread_mutex_unlock(&lock);
    pthread_mutex_unlock(&output_lock);
}
void vibration_shutdown(void) {
    pthread_mutex_lock(&output_lock);
    pthread_mutex_lock(&lock);
    if (output_pad) {
        accepting = focused_snapshot = false;
        neutral_locked();
        output_locked(SDL_GetTicksNS(), true);
        output_pad = NULL;
        have_last = retry = false;
    }
    running = false;
    notify_locked();
    pthread_mutex_unlock(&lock);
    pthread_mutex_unlock(&output_lock);
    if (started) pthread_join(worker, NULL);
    pthread_mutex_lock(&lock);
    started = false;
#ifdef _WIN32
    if (wake_event) CloseHandle(wake_event);
    wake_event = NULL;
#endif
    pthread_mutex_unlock(&lock);
}
void vibration_update(SDL_Gamepad *pad, bool focused) {
    static bool configured;
    if (!configured) {
        SDL_SetHintWithPriority("SDL_JOYSTICK_HIDAPI_SWITCH2_EXPERIMENTAL_HD", "1", SDL_HINT_DEFAULT);
        configured = true;
    }
    if (!pad || !focused || !g_settings.rumble) {
        pthread_mutex_lock(&lock);
        accepting = false;
        neutral_locked();
        pthread_mutex_unlock(&lock);
        betop_haptics_clear();
    }
    bool enhanced_allowed = betop_game_state_update(pad, focused && g_settings.rumble && g_settings.rumble_strength > 0 && vibration_permitted());
    betop_haptics_update(pad, enhanced_allowed);
    pthread_mutex_lock(&output_lock);
    pthread_mutex_lock(&lock);
    if (pad != output_pad) {
        output_pad = pad;
        have_last = retry = false;
        submitted_ns = retry_after_ns = 0;
    }
    focused_snapshot = focused;
    accepting = pad && focused && g_settings.rumble;
    strength_snapshot = g_settings.rumble_strength;
    heartbeat_ns = SDL_GetTicksNS();
#ifdef VIBRATION_MANUAL_TEST
    stop_pending = false;
    output_locked(heartbeat_ns, true);
#else
    if (pad && !started) {
#ifdef _WIN32
        wake_event = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (!wake_event) {
            LOG("[input] vibration wake event creation failed\n");
            pthread_mutex_unlock(&lock);
            pthread_mutex_unlock(&output_lock);
            return;
        }
#endif
        running = true;
        int error = pthread_create(&worker, NULL, vibration_worker, NULL);
        started = error == 0;
        if (error) {
            running = false;
#ifdef _WIN32
            CloseHandle(wake_event);
            wake_event = NULL;
#endif
            LOG("[input] vibration worker creation failed: %d\n", error);
        }
    }
#endif
    notify_locked();
    pthread_mutex_unlock(&lock);
    pthread_mutex_unlock(&output_lock);
}
