// Synthetic HID -> SDL checks, with no game files or physical controller. make test-vibration
#include <assert.h>
#include <stdio.h>
#include "../runtime/hle/vibration.c"
#include "../runtime/services/hid_set.c"

Settings g_settings = {.rumble = true, .rumble_strength = 10};

static uint64_t now;
static unsigned calls;
static uint16_t motor_low, motor_high;
static uint32_t duration;
static VibrationValue bands[2];
static bool enhanced_allowed;
static unsigned enhanced_updates, enhanced_clears;
bool betop_game_state_update(SDL_Gamepad *pad, bool allowed) { return pad && allowed; }
void betop_haptics_update(SDL_Gamepad *pad, bool allowed) {
    enhanced_allowed = pad && allowed;
    ++enhanced_updates;
}
void betop_haptics_clear(void) { enhanced_allowed = false; ++enhanced_clears; }
bool SDL_SetHintWithPriority(const char *name, const char *value, SDL_HintPriority priority) { return true; }
Uint64 SDL_GetTicks(void) { return now; }
bool SDL_RumbleGamepadFrequenciesEx(SDL_Gamepad *pad,
        float la, float lf, float lh, float lhf, float ra, float rf, float rh, float rhf, Uint32 ms) {
    assert(pad);
    bands[0] = (VibrationValue){la, lf, lh, lhf};
    bands[1] = (VibrationValue){ra, rf, rh, rhf};
    motor_low = (Uint16)lroundf(fmaxf(la, ra) * 65535);
    motor_high = (Uint16)lroundf(fmaxf(lh, rh) * 65535);
    duration = ms; calls++;
    return true;
}
const char *SDL_GetError(void) { return "test"; }

// Unrelated HID / settings dependencies. Keep the returned vibration object callable.
static Service returned_service;
Service *service_new(const char *name, ServiceHandler handler, void *state) {
    returned_service = (Service){.name = name, .handler = handler, .state = state};
    return &returned_service;
}
void ipc_return_object(IpcResp *resp, Service *s) { resp->objects[resp->nobjects++] = s; }
KSharedMem *kernel_create_shared_memory(uint64_t size, void (*on_map)(KSharedMem *), uint32_t *handle) {
    (void)size; (void)on_map; (void)handle; abort();
}
KEvent *kernel_create_event_pair(uint32_t *w, uint32_t *r) { (void)w; (void)r; abort(); }
const char *language_text_code(void) { return "en-US"; }

static IpcResp command(uint32_t cmd, const void *data, uint32_t size) {
    IpcReq req = {.cmd = cmd, .data = data, .data_size = size};
    IpcResp resp = {0};
    hid(NULL, &req, &resp);
    return resp;
}

int main(void) {
    const uint32_t left = 0x00000003, right = 0x00010003;
    SDL_Gamepad *pad = (SDL_Gamepad *)(uintptr_t)1;
    vibration_reset();
    IpcResp resp = command(205, NULL, 0);
    assert(resp.data_size == 1 && resp.data[0] == 1);
    resp = command(203, NULL, 0);
    assert(resp.nobjects == 1 && resp.objects[0]->handler == active_vibration_devices);
    IpcReq activate = {0}; IpcResp activated = {0};
    resp.objects[0]->handler(resp.objects[0], &activate, &activated);
    assert(activated.result == 0);
    resp = command(200, &right, sizeof right);
    assert(resp.data_size == 8 && resp.data[0] == 1 && resp.data[4] == 2);

    // Single-device command: the value starts at byte 4, not at the aligned user id at byte 24.
    uint8_t packet[32] = {0};
    VibrationValue value = {.25f, 160, .5f, 320};
    memcpy(packet, &left, 4); memcpy(packet + 4, &value, sizeof value);
    command(201, packet, sizeof packet);
    vibration_update(pad, true);
    assert(motor_low == 16384 && motor_high == 32768 && duration == 100);
    unsigned before = calls;
    now = 49; vibration_update(pad, true); assert(calls == before);
    assert(enhanced_updates == 2 && enhanced_allowed); // Enhanced deadlines bypass grip deduplication.
    now = 50; vibration_update(pad, true); assert(calls == before + 1);
    resp = command(202, packet, 16);
    assert(resp.data_size == sizeof value && memcmp(resp.data, &value, sizeof value) == 0);
    command(201, packet, 4);  // truncated: leave the current effect alone
    assert(vibration_get(left).low_amplitude == .25f);

    uint32_t handles[] = {left, right};
    VibrationValue batch[] = {{.75f, 100, .1f, 200}, {.2f, 180, .9f, 400}};
    IpcReq req = {.cmd = 206, .nx = 2, .x = {
        {(uintptr_t)handles, sizeof handles}, {(uintptr_t)batch, sizeof batch}}};
    resp = (IpcResp){0}; hid(NULL, &req, &resp);
    vibration_update(pad, true);
    assert(motor_low == 49151 && motor_high == 58982);
    assert(memcmp(bands, batch, sizeof bands) == 0);
    before = calls;
    batch[1].high_frequency = 410;  // frequency-only changes must reach the driver immediately
    vibration_send(right, &batch[1]); vibration_update(pad, true);
    assert(calls == before + 1 && bands[1].high_frequency == 410);
    // Bound iteration by the shorter buffer; an incomplete trailing value must not be read.
    batch[0].low_amplitude = .6f; req.x[1].size = sizeof(VibrationValue) + 1;
    hid(NULL, &req, &resp);
    assert(vibration_get(left).low_amplitude == .6f && vibration_get(right).high_amplitude == .9f);

    // The player's toggle stops active motors without changing the game's HID permission.
    g_settings.rumble = false;
    vibration_update(pad, true);
    assert(motor_low == 0 && motor_high == 0 && vibration_permitted());
    assert(!enhanced_allowed);  // The player toggle also gates semantic HD effects.
    command(201, packet, sizeof packet);
    assert(vibration_get(left).low_amplitude == 0);
    vibration_reset();
    assert(!g_settings.rumble);  // returning to the title screen preserves the preference
    g_settings.rumble = true;
    vibration_update(pad, true);
    assert(motor_low == 0 && motor_high == 0);  // no stale effect after re-enabling
    command(201, packet, sizeof packet);
    vibration_update(pad, true);
    assert(motor_low == 16384 && motor_high == 32768);

    // User strength changes reach SDL immediately without rewriting guest values.
    before = calls;
    g_settings.rumble_strength = RUMBLE_STRENGTH_DEFAULT;
    vibration_update(pad, true);
    assert(calls == before + 1 && bands[0].low_amplitude == .5f && bands[0].high_amplitude == 1);
    assert(vibration_get(left).low_amplitude == .25f);
    g_settings.rumble_strength = 0;
    vibration_update(pad, true);
    assert(motor_low == 0 && motor_high == 0 && duration == 0 && !enhanced_allowed);
    g_settings.rumble_strength = 10;
    vibration_update(pad, true);
    assert(motor_low == 16384 && motor_high == 32768);
    // Same-sided bands share saturation, while the other side is independent.
    float low = .125f, high = .0625f;
    rumble_apply_strength(&low, &high, 100);
    assert(low == 1 && high == .5f);
    low = .025f; high = 0;
    rumble_apply_strength(&low, &high, 50);
    assert(fabsf(low - .125f) < 1e-6f && high == 0);
    assert(rumble_strength_percent(-1) == 0 && rumble_strength_percent(101) == 100);

    uint8_t allow = 0; command(204, &allow, 1);
    vibration_update(pad, true); assert(motor_low == 0 && motor_high == 0);
    assert(!enhanced_allowed && enhanced_clears);
    command(201, packet, sizeof packet);
    assert(vibration_get(left).low_amplitude == 0);
    allow = 1; command(204, &allow, 1);
    vibration_update(pad, true); assert(motor_low == 0);  // do not resume an old effect

    value = (VibrationValue){NAN, 160, INFINITY, 320};
    vibration_send(left, &value); vibration_update(pad, true);
    assert(motor_low == 0 && motor_high == 0);
    value = (VibrationValue){1, NAN, 1, 320};
    vibration_send(left, &value); vibration_update(pad, true);
    assert(bands[0].low_amplitude == 0 && bands[0].low_frequency == 160);
    value = (VibrationValue){-1, 160, 2, 320};
    vibration_send(left, &value); vibration_update(pad, true);
    assert(motor_low == 0 && motor_high == 65535);
    value = (VibrationValue){1, 160, 1, 320};
    vibration_send(0x00000103, &value);  // player 2: must not drive player 1
    assert(vibration_get(left).low_amplitude == 0);
    vibration_send(0, &value); assert(vibration_get(left).low_amplitude == 0);
    vibration_update(pad, false); assert(motor_high == 0);
    assert(!enhanced_allowed);
    vibration_update(pad, true); assert(motor_high == 0);
    vibration_send(0x00002003, &value);  // handheld aliases the same physical controller
    vibration_update(pad, true); assert(motor_low == 65535);
    vibration_update(NULL, false);
    vibration_update(pad, true); assert(vibration_get(left).low_amplitude == 0);
    vibration_send(left, &value); command(210, NULL, 0);
    vibration_update(pad, true); assert(motor_low == 0);
    vibration_send(left, &value); vibration_reset();
    vibration_update(pad, true); assert(motor_low == 0 && vibration_permitted());
    puts("vibration: HID packets, batching, motors, focus, disconnect and reboot checks passed");
    return 0;
}
