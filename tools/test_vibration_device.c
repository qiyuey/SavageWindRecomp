// Opt-in synthetic device acceptance. No game files or saves are read.
#include <stdio.h>
#include "../runtime/hle/vibration.c"
Settings g_settings = {.rumble = true, .rumble_strength = 20};
int main(int argc, char **argv) {
    bool play = argc == 2 && !strcmp(argv[1], "--play");
    if (argc > 1 && !play) { puts("Usage: test_vibration_device [--play]"); return 2; }
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_SWITCH2_EXPERIMENTAL_HD", "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (play) {
        SDL_SetHint("SDL_JOYSTICK_HIDAPI_RUMBLE_TRACE", "1");
        SDL_SetLogPriority(SDL_LOG_CATEGORY_INPUT, SDL_LOG_PRIORITY_INFO);
    }
    if (!SDL_Init(SDL_INIT_GAMEPAD)) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    int count;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    SDL_JoystickID selected = 0;
    for (int i = 0; i < count; ++i) {
        Uint16 vendor = SDL_GetGamepadVendorForID(ids[i]), product = SDL_GetGamepadProductForID(ids[i]);
        printf("device %u: %s %04x:%04x\n", ids[i], SDL_GetGamepadNameForID(ids[i]), vendor, product);
        if (vendor == 0x057e && product == 0x2069) selected = ids[i];
    }
    SDL_free(ids);
    if (!play) { puts("Enumeration only; --play runs a one-second low-amplitude Switch 2 Pro test."); SDL_Quit(); return 0; }
    if (!selected) { puts("SKIP: no Switch 2 Pro 057e:2069 connected"); SDL_Quit(); return 77; }
    SDL_Gamepad *pad = SDL_OpenGamepad(selected);
    if (!pad) { fprintf(stderr, "%s\n", SDL_GetError()); SDL_Quit(); return 1; }
    uint32_t handles[] = {3, 0x10003};
    uint64_t begin = SDL_GetTicksNS();
    for (unsigned i = 0; i < 250; ++i) {
        vibration_update(pad, true);
        float a = .02f + .03f * (i % 25) / 24.0f;
        VibrationValue pair[] = {{a, 160, .02f, 320}, {.02f, 180, a, 360}};
        vibration_send_batch(handles, pair, 2);
        uint64_t deadline = begin + (uint64_t)(i + 1) * 4000000;
        uint64_t now = SDL_GetTicksNS();
        if (now < deadline) SDL_DelayPrecise(deadline - now);
    }
    vibration_update(pad, false);
    SDL_Delay(20);
    vibration_detach(pad);
    vibration_shutdown();
    SDL_CloseGamepad(pad);
    SDL_Quit();
    puts("Completed synthetic device output and stop; inspect HIDAPI trace for transfer results.");
    puts("Physical feel and gameplay effects require human acceptance.");
    return 0;
}
