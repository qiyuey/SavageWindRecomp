// Authored Breath of the Wild contact-effect policy. No device report bytes.
// Only exact Player-owned accepted requests enter this table. The original
// game's grip rumble is independent and is never replaced by these profiles.
#pragma once

enum {
    BETOP_LEFT = 1, BETOP_RIGHT = 2, BETOP_BOTH = 3,
    BOTW_NORMAL_HIT = 1, BOTW_IMPACT_HIT = 2, BOTW_ARROW_HIT = 3,
    BOTW_GUARD = 4, BOTW_GUARD_JUST = 5, BOTW_STASIS_HIT = 6,
    BOTW_WIND_BARRIER_HIT = 7, BOTW_MONSTER_PUNCH = 8,
    BOTW_WEAPON_RAISE_SHOCK = 9, BOTW_WOODCUTTER = 10,
    BOTW_TREE_SPLIT = 11, BOTW_PLAYER_LAND = 19,
    BOTW_STASIS_START = 27, BOTW_STASIS_END = 28,
    BOTW_RUMBLE_TYPE_COUNT = 29
};

typedef struct {
    unsigned short ms;
    unsigned char sides, trigger_force, priority;
} BetopProfile;

// Duration in milliseconds; trigger_force is the SDL mode-2 protocol parameter,
// not a physical force unit. Shoulder output is binary direct drive. These
// profiles are integrated but still need in-game perceptual tuning. Do not
// infer bow draw/fire state from ArrowHit, or shield-holding state from Guard.
static const BetopProfile profiles[BOTW_RUMBLE_TYPE_COUNT] = {
    [BOTW_NORMAL_HIT]         = { 90, BETOP_RIGHT, 15, 2},
    [BOTW_IMPACT_HIT]         = {160, BETOP_BOTH,  25, 3},
    [BOTW_ARROW_HIT]          = { 70, BETOP_RIGHT, 12, 2},
    [BOTW_GUARD]              = {100, BETOP_LEFT,  18, 2},
    [BOTW_GUARD_JUST]         = {180, BETOP_LEFT,  25, 4},
    [BOTW_STASIS_HIT]         = {100, BETOP_BOTH,   0, 2},
    [BOTW_WIND_BARRIER_HIT]   = {120, BETOP_LEFT,   0, 2},
    [BOTW_MONSTER_PUNCH]      = {150, BETOP_BOTH,  20, 3},
    [BOTW_WEAPON_RAISE_SHOCK] = { 80, BETOP_RIGHT,  0, 1},
    [BOTW_WOODCUTTER]         = { 90, BETOP_RIGHT, 12, 2},
    [BOTW_TREE_SPLIT]         = {130, BETOP_BOTH,   0, 2},
    [BOTW_PLAYER_LAND]        = { 80, BETOP_BOTH,   0, 1},
    [BOTW_STASIS_START]       = { 90, BETOP_LEFT,   0, 1},
    [BOTW_STASIS_END]         = { 90, BETOP_RIGHT,  0, 1},
};
