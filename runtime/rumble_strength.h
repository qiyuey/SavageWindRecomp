// Application preference only; device response and protocol encoding belong to SDL.
#pragma once
#include <math.h>

#define RUMBLE_STRENGTH_DEFAULT 50
#define RUMBLE_BASE_GAIN 5.0f

static inline int rumble_strength_percent(int percent) {
    return percent < 0 ? 0 : percent > 100 ? 100 : percent;
}

static inline void rumble_apply_strength(float *low, float *high, int percent) {
    float gain = RUMBLE_BASE_GAIN * rumble_strength_percent(percent) / 100.0f;
    // Shared headroom preserves the two components' ratio when one saturates.
    float peak = fmaxf(*low, *high) * gain;
    if (peak > 1.0f) gain /= peak;
    *low *= gain;
    *high *= gain;
}
