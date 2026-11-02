#include "nvn/astc.h"
#include <SDL3/SDL_timer.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void vk_astc_decode_ldr(const uint8_t *, uint32_t, uint32_t, uint32_t, uint32_t, bool, uint8_t *);
static void check(uint32_t w, uint32_t h, uint32_t bw, uint32_t bh, bool srgb) {
    uint8_t pool[256][16], tmp[12 * 12 * 4];
    uint32_t rng = 123456789;
    for (int i = 0; i < 256; i++) {
        do {
            for (int j = 0; j < 16; j++) {
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                pool[i][j] = (uint8_t)rng;
            }
        } while (!astc_decode_block(pool[i], bw, bh, srgb, tmp));
    }
    size_t n = (size_t)((w + bw - 1) / bw) * ((h + bh - 1) / bh);
    uint8_t *blocks = malloc(n * 16), *ref = malloc((size_t)w*h*4), *got = malloc((size_t)w*h*4 + 128);
    assert(blocks && ref && got);
    for (size_t i = 0; i < n; i++) memcpy(blocks + i*16, pool[i%256], 16);
    memset(got, 0xcc, (size_t)w*h*4 + 128);
    uint64_t start = SDL_GetTicksNS();
    astc_decode_ldr(blocks, w, h, bw, bh, srgb, ref);
    uint64_t serial = SDL_GetTicksNS() - start;
    start = SDL_GetTicksNS();
    vk_astc_decode_ldr(blocks, w, h, bw, bh, srgb, got + 64);
    uint64_t parallel = SDL_GetTicksNS() - start;
    assert(!memcmp(ref, got + 64, (size_t)w*h*4));
    for (int i = 0; i < 64; i++) assert(got[i] == 0xcc && got[64+(size_t)w*h*4+i] == 0xcc);
    if (w == 1024) printf("1024x768 ASTC %ux%u srgb=%d: serial %.2f ms, pool %.2f ms\n", bw,bh,srgb,serial/1e6,parallel/1e6);
    free(blocks); free(ref); free(got);
}
static void *concurrent(void *unused) { (void)unused; for (int i=0;i<4;i++) check(521,513,6,5,i&1); return NULL; }
int main(void) {
    const uint32_t shapes[][2] = {{4,4},{5,4},{6,5},{8,8},{12,12}};
    for (int s=0;s<5;s++) for(int srgb=0;srgb<2;srgb++) {
        check(17,19,shapes[s][0],shapes[s][1],srgb);
        check(521,513,shapes[s][0],shapes[s][1],srgb);
        check(1024,768,shapes[s][0],shapes[s][1],srgb);
    }
    pthread_t t[2];
    for(int i=0;i<2;i++) assert(!pthread_create(&t[i],NULL,concurrent,NULL));
    for(int i=0;i<2;i++) pthread_join(t[i],NULL);
    puts("PASS: serial/parallel byte equality, partial blocks, sRGB, guards and concurrent callers");
}
