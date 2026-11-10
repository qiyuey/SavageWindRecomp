// Application preference only; device response and protocol encoding belong to SDL.
#pragma once
#include <math.h>

#define RUMBLE_STRENGTH_MIN 0
#define RUMBLE_STRENGTH_MAX 100
#define RUMBLE_STRENGTH_DEFAULT 50
#define RUMBLE_BASE_GAIN 5.0f

static inline int rumble_strength_percent(int percent) {
    return percent < RUMBLE_STRENGTH_MIN ? RUMBLE_STRENGTH_MIN :
           percent > RUMBLE_STRENGTH_MAX ? RUMBLE_STRENGTH_MAX : percent;
}

static inline void rumble_apply_strength(float *low, float *high, int percent) {
    float gain = RUMBLE_BASE_GAIN * rumble_strength_percent(percent) / 100.0f;
    // Shared headroom preserves the two components' ratio when one saturates.
    float peak = fmaxf(*low, *high) * gain;
    if (peak > 1.0f) gain /= peak;
    *low *= gain;
    *high *= gain;
}
