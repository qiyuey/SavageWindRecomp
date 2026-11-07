// BC5 (NVN format 0x4C) view format: SAVAGEWIND_BC5_UNORM picks BC5_UNORM, else BC5_SNORM. The environment is read
// once (the first fmt_of call), so changing it later in the process must not change the format.
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>

#include "../runtime/nvn/vulkan/texture_formats.h"

static void set_unorm(int on) {
#ifdef _WIN32
    _putenv_s("SAVAGEWIND_BC5_UNORM", on ? "1" : "");
#else
    if (on) setenv("SAVAGEWIND_BC5_UNORM", "1", 1);
    else unsetenv("SAVAGEWIND_BC5_UNORM");
#endif
}

static double now_ns(void) { return (double)clock() * (1e9 / CLOCKS_PER_SEC); }

int main(int argc, char **argv) {
    bool want_unorm = getenv("SAVAGEWIND_BC5_UNORM") != NULL;
    VkFormat want = want_unorm ? VK_FORMAT_BC5_UNORM_BLOCK : VK_FORMAT_BC5_SNORM_BLOCK;

    assert(fmt_of(0x4C).vf == want);
    assert(!fmt_of(0x4C).alpha_one);

    set_unorm(!want_unorm);
    assert((getenv("SAVAGEWIND_BC5_UNORM") != NULL) != want_unorm);
    assert(fmt_of(0x4C).vf == want);

    assert(fmt_of(0x4A).vf == VK_FORMAT_BC4_SNORM_BLOCK);
    assert(fmt_of(0x4B).vf == VK_FORMAT_BC5_UNORM_BLOCK);
    assert(fmt_of(0x4D).vf == VK_FORMAT_BC7_UNORM_BLOCK);
    assert(fmt_of(0x4E).vf == VK_FORMAT_BC7_SRGB_BLOCK);

    puts("PASS: 0x4C honours SAVAGEWIND_BC5_UNORM and reads it only once");

    if (argc > 1 && !strcmp(argv[1], "--bench")) {
        const int n = 1000000;
        const char *volatile key = "SAVAGEWIND_BC5_UNORM";  // volatile: not hoisted out of the loop
        volatile uintptr_t sink = 0;
        double t0 = now_ns();
        for (int i = 0; i < n; i++) sink ^= (uintptr_t)fmt_of(0x4C).vf;
        double cached = (now_ns() - t0) / n;
        t0 = now_ns();
        for (int i = 0; i < n; i++) sink ^= (uintptr_t)getenv(key);
        double raw = (now_ns() - t0) / n;
        printf("fmt_of(0x4C).vf: %.2f ns/call; getenv: %.2f ns/call (%.0fx)\n", cached, raw, raw / cached);
        (void)sink;
    }
    return 0;
}
