// No game files or physical device: accepted semantic events -> authored effects.
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "../runtime/betop_haptics.c"

static Uint64 clock_ms;
static unsigned calls;
static Uint32 output_lease;
static Uint64 output_valid_until;
static SDL_BetopGamepadEffects output;
static bool fail_output;
static SDL_Gamepad *const kp70 = (SDL_Gamepad *)(uintptr_t)1;
static SDL_Gamepad *const other = (SDL_Gamepad *)(uintptr_t)2;
Uint64 SDL_GetTicks(void) { return clock_ms; }
Uint16 SDL_GetGamepadVendor(SDL_Gamepad *pad) { return pad == kp70 ? 0x20bc : 0x045e; }
Uint16 SDL_GetGamepadProduct(SDL_Gamepad *pad) { return pad == kp70 ? 0x5174 : 0x02e0; }
const char *SDL_GetError(void) { return "mock transport failure"; }
bool SDL_SetGamepadBetopEffects(SDL_Gamepad *pad, const SDL_BetopGamepadEffects *effects, Uint32 ms) {
    assert(pad == kp70);
    assert(!has_effects(effects) || (ms >= 1 && ms <= 100));
    output = *effects;
    output_lease = ms;
    output_valid_until = clock_ms + ms;
    ++calls;
    return !fail_output;
}

static void test_actions(void) {
    betop_haptics_clear();
    betop_haptics_update(kp70, true);
    assert(!has_effects(&output));
    unsigned before = calls;
    rt_betop_action_state(BETOP_ACTION_BOW_DRAW, -1);
    betop_haptics_update(kp70, true);
    assert(calls == before + 1 && output_lease == 100);
    assert(output.triggers[1].mode == SDL_BETOP_TRIGGER_RESISTANCE);
    assert(output.triggers[1].parameters[0] == 10 && output.triggers[1].parameters[1] == 15);
    assert(!output.triggers[0].mode && !output.shoulders[0].preset && !output.shoulders[1].preset);

    // Fresh snapshots and submissions stay alive beyond SDL's 2 s refresh.
    before = calls;
    for (unsigned i = 0; i < 120; ++i) {
        clock_ms += 25;
        rt_betop_action_state(BETOP_ACTION_BOW_DRAW, -1);
        betop_haptics_update(kp70, true);
        assert(output.triggers[1].mode == SDL_BETOP_TRIGGER_RESISTANCE);
        assert(output_valid_until > clock_ms);
    }
    assert(calls >= before + 60);

    rt_betop_action_state(BETOP_ACTION_BOW_DRAW, 0);
    betop_haptics_update(kp70, true); assert(output.triggers[1].parameters[1] == 10);
    rt_betop_action_state(BETOP_ACTION_BOW_DRAW, .5f);
    betop_haptics_update(kp70, true); assert(output.triggers[1].parameters[1] == 18);
    rt_betop_action_state(BETOP_ACTION_BOW_DRAW | BETOP_ACTION_SHIELD_GUARD, 1);
    betop_haptics_update(kp70, true);
    assert(output.triggers[1].parameters[1] == 25);
    assert(output.triggers[0].mode == SDL_BETOP_TRIGGER_RESISTANCE && output.triggers[0].parameters[1] == 10);

    // Contact shoulders coexist; neither contact trigger replaces action force.
    rt_betop_rumble_event(BOTW_IMPACT_HIT, "Player");
    betop_haptics_update(kp70, true);
    assert(output.shoulders[0].preset == SDL_BETOP_SHOULDER_DIRECT && output.shoulders[1].preset == SDL_BETOP_SHOULDER_DIRECT);
    assert(output.triggers[0].mode == SDL_BETOP_TRIGGER_RESISTANCE && output.triggers[1].mode == SDL_BETOP_TRIGGER_RESISTANCE);
    clock_ms += 100;
    rt_betop_action_state(BETOP_ACTION_BOW_DRAW | BETOP_ACTION_SHIELD_GUARD, 1);
    betop_haptics_update(kp70, true);
    assert(output_lease == 60); // Earliest contact expiry, not action expiry.
    clock_ms += 60;
    rt_betop_action_state(BETOP_ACTION_BOW_DRAW | BETOP_ACTION_SHIELD_GUARD, 1);
    betop_haptics_update(kp70, true);
    assert(!output.shoulders[0].preset && !output.shoulders[1].preset);
    assert(output.triggers[0].mode == SDL_BETOP_TRIGGER_RESISTANCE && output.triggers[1].mode == SDL_BETOP_TRIGGER_RESISTANCE);

    // Unknown progress never leaks NaN to a protocol byte or fabricates full draw.
    rt_betop_action_state(BETOP_ACTION_BOW_DRAW, NAN);
    betop_haptics_update(kp70, true); assert(output.triggers[1].parameters[1] == 15 && !output.triggers[0].mode);
    rt_betop_action_state(0, -1);
    betop_haptics_update(kp70, true); assert(!has_effects(&output) && output_lease == 0);

    // Snapshot expiry removes force even when the event loop keeps pumping.
    rt_betop_action_state(BETOP_ACTION_BOW_DRAW, -1);
    betop_haptics_update(kp70, true);
    clock_ms += 100; betop_haptics_update(kp70, true); assert(output_lease == 50);
    clock_ms += 49; betop_haptics_update(kp70, true); assert(output_lease == 1);
    ++clock_ms; betop_haptics_update(kp70, true); assert(!has_effects(&output));
    rt_betop_action_state(BETOP_ACTION_SHIELD_GUARD, -1);
    betop_haptics_update(kp70, true);
    betop_haptics_update(kp70, false); assert(!has_effects(&output));
    rt_betop_action_state(BETOP_ACTION_BOW_DRAW, 1); // Unfocused: discard.
    before = calls;
    betop_haptics_update(kp70, true); assert(calls == before);
    rt_betop_action_state(BETOP_ACTION_BOW_DRAW, 1);
    betop_haptics_update(kp70, true);
    betop_haptics_clear();
    rt_betop_action_state(BETOP_ACTION_BOW_DRAW, 1); // Reset: discard.
    betop_haptics_update(kp70, true); assert(!has_effects(&output));
    rt_betop_action_state(BETOP_ACTION_BOW_DRAW, 1);
    betop_haptics_update(kp70, true);
    rt_betop_action_state(4, 1); // Unrecognized flags fail closed.
    betop_haptics_update(kp70, true); assert(!has_effects(&output));

    setenv("SAVAGEWIND_BETOP", "0", 1);
    betop_haptics_update(kp70, true);
    rt_betop_action_state(BETOP_ACTION_BOW_DRAW, 1);
    unsetenv("SAVAGEWIND_BETOP");
    before = calls;
    betop_haptics_update(kp70, true); assert(calls == before);
    rt_betop_action_state(BETOP_ACTION_BOW_DRAW, 1);
    betop_haptics_update(kp70, true);
    betop_haptics_update(NULL, false); // The input layer/SDL owns stop-before-close.
    rt_betop_action_state(BETOP_ACTION_BOW_DRAW, 1);
    before = calls;
    betop_haptics_update(kp70, true); assert(calls == before && !action_flags);
}

int main(void) {
    unsetenv("SAVAGEWIND_BETOP");
    rt_betop_rumble_event(5, "Player"); // Before first connected/focused update: ignore.
    betop_haptics_update(kp70, true);
    assert(calls == 0);
    rt_betop_rumble_event(5, "Enemy_Bokoblin");
    rt_betop_rumble_event(5, "Weapon_Sword_001");
    rt_betop_rumble_event(5, "PlayerOther");
    rt_betop_rumble_event(5, NULL);
    rt_betop_rumble_event(29, "Player");
    betop_haptics_update(kp70, true);
    assert(calls == 0);
    rt_betop_rumble_event(5, "Player");
    betop_haptics_update(kp70, true);
    assert(calls == 1 && output.shoulders[0].preset == 4 && output.shoulders[0].strength == 255);
    assert(output.triggers[0].mode == 2 && output.triggers[0].parameters[1] == 25);
    assert(!output.shoulders[1].preset && output_lease == 100);
    clock_ms = 49; betop_haptics_update(kp70, true); assert(calls == 1);
    clock_ms = 50; betop_haptics_update(kp70, true); assert(calls == 2 && output_lease == 100);
    clock_ms = 150; betop_haptics_update(kp70, true); assert(output_lease == 30);
    clock_ms = 180; betop_haptics_update(kp70, true); assert(!has_effects(&output) && output_lease == 0);
    unsigned before = calls;
    betop_haptics_update(kp70, true); assert(calls == before);

    // Independent sides and priority preserve a defensive effect during a hit.
    rt_betop_rumble_event(5, "Player");
    rt_betop_rumble_event(2, "Player");
    rt_betop_rumble_event(19, "Player");
    betop_haptics_update(kp70, true);
    assert(output.shoulders[0].preset == 4 && output.shoulders[1].preset == 4);
    assert(output.triggers[0].parameters[1] == 25);
    // Right impact expires before the higher-priority left deflection.
    clock_ms += 160;
    betop_haptics_update(kp70, true);
    assert(output.shoulders[0].preset == 4 && !output.shoulders[1].preset);
    betop_haptics_update(kp70, false);
    assert(!has_effects(&output));
    before = calls;
    rt_betop_rumble_event(2, "Player");
    betop_haptics_update(kp70, true); assert(calls == before); // No focus-loss replay.

    rt_betop_rumble_event(1, "Player");
    clock_ms += 100; betop_haptics_update(kp70, true); assert(calls == before); // Stale event.
    rt_betop_rumble_event(3, "Player");
    betop_haptics_update(kp70, true); assert(output.shoulders[1].preset == 4);
    betop_haptics_clear();
    rt_betop_rumble_event(3, "Player");
    betop_haptics_update(kp70, true); assert(!has_effects(&output));
    before = calls;
    betop_haptics_update(other, true);
    rt_betop_rumble_event(5, "Player");
    betop_haptics_update(other, true); assert(calls == before);
    betop_haptics_update(NULL, false);
    betop_haptics_update(kp70, true); assert(calls == before);

    setenv("SAVAGEWIND_BETOP", "0", 1);
    betop_haptics_update(kp70, true);
    rt_betop_rumble_event(5, "Player");
    betop_haptics_update(kp70, true); assert(calls == before);
    unsetenv("SAVAGEWIND_BETOP");
    betop_haptics_update(kp70, true);
    fail_output = true;
    rt_betop_rumble_event(5, "Player");
    betop_haptics_update(kp70, true); assert(calls == before + 1);
    fail_output = false;
    rt_betop_rumble_event(5, "Player");
    clock_ms += 999; betop_haptics_update(kp70, true); assert(calls == before + 1);
    ++clock_ms; betop_haptics_update(kp70, true); assert(calls == before + 1);
    rt_betop_rumble_event(4, "Player");
    betop_haptics_update(kp70, true); assert(calls == before + 2 && output.shoulders[0].preset == 4);
    clock_ms += 80; betop_haptics_update(kp70, true); assert(output_lease == 20);
    before = calls;
    clock_ms += 11; rt_betop_rumble_event(4, "Player");
    betop_haptics_update(kp70, true); assert(calls == before + 1 && output_lease == 100);
    test_actions();
    puts("betop events/actions: filtering, priority, sustained bow/guard, progress, expiry, cancellation, leases, focus, reset, stale events and retry passed");
    return 0;
}
