// Original game rumble remains in hle/vibration.c. These additional, authored
// trigger/shoulder pulses are not Nintendo HD-rumble data and do not infer
// gameplay from buttons. The generator calls us only after a named effect was
// accepted by the game's dispatcher. Only the exact Player actor is enhanced;
// weapon-owned and NPC events retain their original grip vibration unchanged.
#include <SDL3/SDL.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "betop_haptics.h"
#include "rt.h"

#include "betop_haptics_profiles.h"

static pthread_mutex_t betop_lock = PTHREAD_MUTEX_INITIALIZER;
static Uint64 deadlines[BOTW_RUMBLE_TYPE_COUNT];
static Uint64 action_deadline;
static unsigned action_flags;
static Uint8 bow_strength;
static bool accepting;
// The following state is also guarded: reset can run on a guest thread.
static SDL_Gamepad *last_pad;
static SDL_BetopGamepadEffects last_effects;
static Uint64 refresh_at, retry_at;
static bool submitted, logged_success, logged_failure;

static bool has_effects(const SDL_BetopGamepadEffects *effects) {
    return effects->triggers[0].mode || effects->triggers[1].mode ||
           effects->shoulders[0].preset || effects->shoulders[1].preset;
}

void betop_haptics_clear(void) {
    pthread_mutex_lock(&betop_lock);
    memset(deadlines, 0, sizeof deadlines);
    action_flags = 0;
    action_deadline = 0;
    accepting = false;
    // Keep last_effects until the event thread submits the stop. If it stalls,
    // SDL's separate haptics worker expires the previously submitted lease.
    pthread_mutex_unlock(&betop_lock);
}

void rt_betop_rumble_event(unsigned type, const char *actor_name) {
    if (type >= SDL_arraysize(profiles) || !profiles[type].ms ||
        !actor_name || strcmp(actor_name, "Player")) return;
    pthread_mutex_lock(&betop_lock);
    if (accepting) deadlines[type] = SDL_GetTicks() + profiles[type].ms;
    pthread_mutex_unlock(&betop_lock);
}

void rt_betop_action_state(unsigned flags, float draw_progress) {
    pthread_mutex_lock(&betop_lock);
    // Unknown state bits must never leave a previously armed effect running.
    if (!accepting || (flags & ~(BETOP_ACTION_BOW_DRAW | BETOP_ACTION_SHIELD_GUARD))) flags = 0;
    action_flags = flags;
    action_deadline = flags ? SDL_GetTicks() + 150 : 0;
    bow_strength = draw_progress >= 0 && draw_progress <= 1 ?
        (Uint8)(10 + draw_progress * 15 + 0.5f) : 15;
    pthread_mutex_unlock(&betop_lock);
}

void betop_haptics_update(SDL_Gamepad *pad, bool allowed) {
    const char *option = getenv("SAVAGEWIND_BETOP");
    bool supported = pad && SDL_GetGamepadVendor(pad) == 0x20bc &&
        (SDL_GetGamepadProduct(pad) == 0x5173 || SDL_GetGamepadProduct(pad) == 0x5174);
    Uint64 now = SDL_GetTicks();
    SDL_BetopGamepadEffects effects = {0};
    unsigned priorities[2] = {0};
    Uint64 next_expiry = 0;
    pthread_mutex_lock(&betop_lock);
    if (pad != last_pad) {
        // The input layer stops before close, and SDL closes its companion too.
        // Never dereference a previously closed SDL_Gamepad here.
        last_pad = pad;
        memset(deadlines, 0, sizeof deadlines);
        action_flags = 0;
        action_deadline = 0;
        memset(&last_effects, 0, sizeof last_effects);
        submitted = logged_success = logged_failure = false;
        retry_at = refresh_at = 0;
    }
    accepting = supported && allowed && (!option || strcmp(option, "0")) && now >= retry_at;
    if (!accepting) memset(deadlines, 0, sizeof deadlines);
    if (!accepting || action_deadline <= now) {
        action_flags = 0;
        action_deadline = 0;
    }
    for (unsigned type = 0; type < SDL_arraysize(profiles); ++type) {
        const BetopProfile *profile = &profiles[type];
        if (deadlines[type] <= now) {
            deadlines[type] = 0;
            continue;
        }
        if (!next_expiry || deadlines[type] < next_expiry) next_expiry = deadlines[type];
        for (unsigned side = 0; side < 2; ++side) {
            // Compose simultaneous events per side; a high-priority shield
            // deflection cannot be replaced by a weak landing/foot contact.
            if (!(profile->sides & (1u << side)) || profile->priority < priorities[side]) continue;
            priorities[side] = profile->priority;
            // Preset-only reports produced no physical response on KP70C.
            // Direct drive (255 on / 0 off) was confirmed on both shoulders.
            // Preserve event timing/routing; do not claim crisp/soft/heavy
            // textures or intermediate amplitudes from this binary drive.
            effects.shoulders[side] = (SDL_BetopShoulderEffect){SDL_BETOP_SHOULDER_DIRECT, SDL_BETOP_SHOULDER_DIRECT_ON};
            memset(&effects.triggers[side], 0, sizeof effects.triggers[side]);
            if (profile->trigger_force) {
                effects.triggers[side].mode = SDL_BETOP_TRIGGER_VIBRATION;
                effects.triggers[side].parameters[0] = 10;
                effects.triggers[side].parameters[1] = profile->trigger_force;
                effects.triggers[side].parameters[2] = profile->trigger_force;
                effects.triggers[side].parameters[3] = 20;
            }
        }
    }
    // Continuous action force owns its trigger, but never masks independently
    // authored shoulder pulses. A stale snapshot cannot rearm after a pause.
    if (action_flags) {
        if (!next_expiry || action_deadline < next_expiry) next_expiry = action_deadline;
        for (unsigned side = 0; side < 2; ++side) {
            unsigned action = side ? BETOP_ACTION_BOW_DRAW : BETOP_ACTION_SHIELD_GUARD;
            if (!(action_flags & action)) continue;
            effects.triggers[side] = (SDL_BetopTriggerEffect){0};
            effects.triggers[side].mode = SDL_BETOP_TRIGGER_RESISTANCE;
            effects.triggers[side].parameters[0] = 10;
            effects.triggers[side].parameters[1] = side ? bow_strength : 10;
        }
    }
    bool active = has_effects(&effects);
    bool changed = memcmp(&effects, &last_effects, sizeof effects) != 0;
    if (!supported || (!active && !submitted) || (!changed && (!active || now < refresh_at))) {
        pthread_mutex_unlock(&betop_lock);
        return;
    }
    // The lease never outlives the next event boundary, even if this event
    // thread stops pumping. Renewing every 50ms preserves longer short pulses.
    Uint32 lease = active ? (Uint32)(next_expiry - now) : 0;
    if (lease > 100) lease = 100;
    if (SDL_SetGamepadBetopEffects(pad, &effects, lease)) {
        last_effects = effects;
        // Short final leases must renew before their own expiry, not only on
        // the normal 50ms cadence (another same-type event may extend them).
        refresh_at = now + (lease < 100 ? lease / 2 : 50);
        submitted = active;
        if (active && !logged_success) {
            LOG("[betop] Player haptics submitted (KP70C trigger/shoulder companion)\n");
            logged_success = true;
        }
    } else {
        if (!logged_failure) {
            LOG("[betop] enhanced output unavailable: %s\n", SDL_GetError());
            logged_failure = true;
        }
        // Do not replay a backlog when the device eventually becomes available.
        memset(deadlines, 0, sizeof deadlines);
        action_flags = 0;
        action_deadline = 0;
        memset(&last_effects, 0, sizeof last_effects);
        accepting = submitted = false;
        retry_at = now + 1000;
    }
    pthread_mutex_unlock(&betop_lock);
}
