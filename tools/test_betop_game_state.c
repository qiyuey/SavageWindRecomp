// Synthetic bounded memory only: never reads game files or drives a controller.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../runtime/betop_game_state.h"
#include "../runtime/betop_haptics.h"

enum { BASE = 0x10000, PINFO = BASE, PLAYER = BASE + 0x200,
       ASLIST = BASE + 0x1000, GROUPS = BASE + 0x1200,
       LOWER_SLOTS = BASE + 0x1400, UPPER_SLOTS = BASE + 0x1600,
       LOWER_NODE = BASE + 0x1900, UPPER_NODE = BASE + 0x1a00,
       PLAYER_NAME = BASE + 0x2000, LOWER_NAME = BASE + 0x2100, UPPER_NAME = BASE + 0x2200 };
static unsigned char memory[0x3000];
static unsigned player_reads;
static bool replace_player;

static bool read_memory(void *context, uint64_t address, void *out, size_t size) {
    (void)context;
    if (address < BASE || address - BASE > sizeof memory || size > sizeof memory - (size_t)(address - BASE)) return false;
    memcpy(out, memory + (size_t)(address - BASE), size);
    if (address == PINFO + 0x60 && ++player_reads == 2 && replace_player) {
        uint64_t other = PLAYER + 8;
        memcpy(out, &other, sizeof other);
    }
    return true;
}
static void put64(unsigned address, uint64_t value) { memcpy(memory + address - BASE, &value, sizeof value); }
static void put32(unsigned address, int32_t value) { memcpy(memory + address - BASE, &value, sizeof value); }
static void name(unsigned address, const char *value) { strcpy((char *)memory + address - BASE, value); }
static void setup(const char *upper, const char *lower) {
    memset(memory, 0, sizeof memory);
    player_reads = 0; replace_player = false;
    put64(PINFO + 0x60, PLAYER); put64(PLAYER + 16, PLAYER_NAME); name(PLAYER_NAME, "Player");
    put64(PLAYER + 0x560, ASLIST); put32(ASLIST + 0xb8, 2); put64(ASLIST + 0xc0, GROUPS);
    put32(GROUPS + 0x20, 1); put64(GROUPS + 0x28, LOWER_SLOTS);
    put32(GROUPS + 0x50 + 0x20, 2); put64(GROUPS + 0x50 + 0x28, UPPER_SLOTS);
    put64(LOWER_SLOTS + 0x18, 1); put64(LOWER_SLOTS, LOWER_NODE); put64(LOWER_NODE + 0x10, LOWER_NAME);
    put64(UPPER_SLOTS + 0x98 + 0x18, 1); put64(UPPER_SLOTS + 0x98, UPPER_NODE); put64(UPPER_NODE + 0x10, UPPER_NAME);
    name(UPPER_NAME, upper); name(LOWER_NAME, lower);
}
static void expect(bool valid, unsigned expected) {
    unsigned flags = 0xffffffffu;
    assert(betop_game_state_sample(read_memory, NULL, PINFO, &flags) == valid);
    assert(flags == expected);
}
int main(void) {
    const char *bows[] = {"BowCharge", "SquatBowCharge", "WallBowChargeL", "WallBowChargeR"};
    for (unsigned i = 0; i < 4; ++i) {
        setup(i < 2 ? bows[i] : "Wait", i >= 2 ? bows[i] : "Wait");
        expect(true, BETOP_ACTION_BOW_DRAW);
    }
    const char *other[] = {"BowShoot", "BowReload", "Scope", "bowcharge", "BowChargeOther", "Unknown", ""};
    for (unsigned i = 0; i < sizeof other / sizeof *other; ++i) {
        setup(other[i], other[i]); expect(true, 0);
    }
    setup("WallBowChargeL", "BowCharge"); expect(true, 0); // Wrong slots are not equivalent.
    setup("Wait", "Wait"); memory[PLAYER + 0xc40 - BASE] = 0x10; expect(true, BETOP_ACTION_SHIELD_GUARD);
    setup("BowCharge", "Wait"); memory[PLAYER + 0xc40 - BASE] = 0x10;
    expect(true, BETOP_ACTION_SHIELD_GUARD | BETOP_ACTION_BOW_DRAW);
    setup("Wait", "Wait"); memory[PLAYER + 0xc40 - BASE] = 0xef; expect(true, 0);
    setup("BowCharge", "Wait"); put32(ASLIST + 0xb8, 1);
    memory[PLAYER + 0xc40 - BASE] = 0x10; expect(true, BETOP_ACTION_SHIELD_GUARD);
    setup("BowCharge", "Wait"); put32(GROUPS + 0x50 + 0x20, 1); expect(true, 0);
    setup("BowCharge", "Wait"); put64(UPPER_SLOTS + 0x98 + 0x18, 0); expect(true, 0);
    setup("BowCharge", "Wait"); put64(PLAYER + 0x560, 0); expect(false, 0);
    setup("BowCharge", "Wait"); put64(ASLIST + 0xc0, UINT64_MAX - 20); expect(false, 0);
    setup("BowCharge", "Wait"); put32(ASLIST + 0xb8, 0); expect(false, 0);
    setup("BowCharge", "Wait"); put32(GROUPS + 0x50 + 0x20, -1); expect(false, 0);
    setup("BowCharge", "Wait"); put64(UPPER_NODE + 0x10, 1); expect(false, 0);
    setup("BowCharge", "Wait"); name(PLAYER_NAME, "PlayerOther"); expect(false, 0);
    setup("BowCharge", "Wait"); memset(memory + UPPER_NAME - BASE, 'x', 32); expect(false, 0);
    setup("BowCharge", "Wait"); replace_player = true; expect(false, 0);
    unsigned flags = 3;
    assert(!betop_game_state_sample(NULL, NULL, PINFO, &flags) && !flags);
    assert(!betop_game_state_sample(read_memory, NULL, PINFO, NULL));
    puts("betop game state: bow/guard combinations, optional slots, charge variants, non-charge states, bounded reads and actor replacement passed");
    return 0;
}
