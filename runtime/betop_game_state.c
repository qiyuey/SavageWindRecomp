// The native Player bow-charge predicate checks these AS slots/names in both
// supported builds. Do not substitute controller input, camera mode or BowShoot.
#include "betop_game_state.h"
#include "betop_haptics.h"
#include <string.h>

static bool read_at(BetopReadMemory read_memory, void *context, uint64_t base,
                    uint64_t offset, void *out, size_t size) {
    return base && offset <= UINT64_MAX - base &&
           size <= UINT64_MAX - (base + offset) &&
           read_memory(context, base + offset, out, size);
}

static bool read_name(BetopReadMemory read_memory, void *context, uint64_t address,
                      char name[32]) {
    // No unbounded guest strlen/strcmp, and no requirement that bytes after a
    // valid string's terminator remain mapped.
    for (unsigned i = 0; i < 32; ++i) {
        if (!read_at(read_memory, context, address, i, &name[i], 1)) return false;
        if (!name[i]) return true;
    }
    return false;
}

static bool slot_name(BetopReadMemory read_memory, void *context, uint64_t groups,
                      unsigned group, unsigned slot, char name[32]) {
    uint64_t slots, node, active, name_pointer;
    int32_t count;
    uint64_t group_offset = (uint64_t)group * 0x50;
    name[0] = 0;
    if (!read_at(read_memory, context, groups, group_offset + 0x20, &count, sizeof count) ||
        count < 0) return false;
    // A structurally valid group can lack this optional animation slot.
    if (count <= (int32_t)slot) return true;
    if (!read_at(read_memory, context, groups, group_offset + 0x28, &slots, sizeof slots)) return false;
    uint64_t slot_offset = (uint64_t)slot * 0x98;
    if (!read_at(read_memory, context, slots, slot_offset + 0x18, &active, sizeof active)) return false;
    if (!active) return true;
    if (!read_at(read_memory, context, slots, slot_offset, &node, sizeof node) ||
        !read_at(read_memory, context, node, 0x10, &name_pointer, sizeof name_pointer)) return false;
    return read_name(read_memory, context, name_pointer, name);
}

bool betop_game_state_sample(BetopReadMemory read_memory, void *context,
                             uint64_t player_info, unsigned *flags) {
    uint64_t player, player_again, name_pointer, as_list, as_list_again, groups;
    int32_t group_count;
    uint8_t player_flags;
    char actor_name[32], upper_name[32] = {0}, lower_name[32];
    if (!flags) return false;
    *flags = 0;
    if (!read_memory ||
        !read_at(read_memory, context, player_info, 0x60, &player, sizeof player) ||
        !read_at(read_memory, context, player, 16, &name_pointer, sizeof name_pointer) ||
        !read_name(read_memory, context, name_pointer, actor_name) || strcmp(actor_name, "Player") ||
        !read_at(read_memory, context, player, 0x560, &as_list, sizeof as_list) ||
        !read_at(read_memory, context, as_list, 0xb8, &group_count, sizeof group_count) ||
        group_count <= 0 ||
        !read_at(read_memory, context, as_list, 0xc0, &groups, sizeof groups) ||
        (group_count > 1 && !slot_name(read_memory, context, groups, 1, 1, upper_name)) ||
        !slot_name(read_memory, context, groups, 0, 0, lower_name) ||
        !read_at(read_memory, context, player, 0xc40, &player_flags, sizeof player_flags) ||
        !read_at(read_memory, context, player_info, 0x60, &player_again, sizeof player_again) ||
        player != player_again ||
        !read_at(read_memory, context, player, 0x560, &as_list_again, sizeof as_list_again) ||
        as_list != as_list_again) return false;
    if (!strcmp(upper_name, "BowCharge") || !strcmp(upper_name, "SquatBowCharge") ||
        !strcmp(lower_name, "WallBowChargeL") || !strcmp(lower_name, "WallBowChargeR"))
        *flags = BETOP_ACTION_BOW_DRAW;
    // Both verified builds' Player::isGuard predicate reads this bit. The
    // caller must gate this layout to verified builds; this is not raw ZL.
    if (player_flags & 0x10) *flags |= BETOP_ACTION_SHIELD_GUARD;
    return true;
}
