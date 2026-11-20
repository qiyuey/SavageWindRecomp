// HID vibration values, submitted by a dedicated worker (runtime/hle/vibration.c).
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct { float low_amplitude, low_frequency, high_amplitude, high_frequency; } VibrationValue;
typedef struct SDL_Gamepad SDL_Gamepad;

void vibration_send(uint32_t handle, const VibrationValue *value);
VibrationValue vibration_get(uint32_t handle);
void vibration_permit(bool permitted);
bool vibration_permitted(void);
void vibration_clear(void);
void vibration_update(SDL_Gamepad *pad, bool focused);

// Event thread: detach before closing the SDL handle; join at shutdown.
void vibration_detach(SDL_Gamepad *pad);
void vibration_shutdown(void);
void vibration_exit(void);
// Publish a HID 206 batch as one stereo snapshot (buffers may be unaligned).
void vibration_send_batch(const void *handles, const void *values, uint64_t count);
