// Parallel ASTC decode, without deferring texture availability to a later frame.
// Work is split on block-row boundaries; each worker writes disjoint pixel rows.
#include "../astc.h"
#include "texdecode.h"
#include <SDL3/SDL_cpuinfo.h>
#include <pthread.h>
#include <stdlib.h>

typedef struct {
    const uint8_t *blocks;
    uint8_t *out;
    uint32_t width, height, bw, bh;
    bool srgb;
} Decode;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_mutex_t submit = PTHREAD_MUTEX_INITIALIZER, lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready = PTHREAD_COND_INITIALIZER, finished = PTHREAD_COND_INITIALIZER;
static Decode job;
static int workers, pending;
static uint64_t generation;

static void stripe(const Decode *d, unsigned part, unsigned parts) {
    uint32_t rows = (d->height + d->bh - 1) / d->bh;
    uint32_t first = (uint64_t)rows * part / parts, last = (uint64_t)rows * (part + 1) / parts;
    if (first == last) return;
    uint32_t y = first * d->bh, end = last * d->bh;
    if (end > d->height) end = d->height;
    size_t columns = (d->width + d->bw - 1) / d->bw;
    astc_decode_ldr(d->blocks + first * columns * 16, d->width, end - y, d->bw, d->bh,
                    d->srgb, d->out + (size_t)y * d->width * 4);
}
static void *worker(void *arg) {
    unsigned part = (unsigned)(uintptr_t)arg;
    uint64_t seen = 0;
    for (;;) {
        pthread_mutex_lock(&lock);
        while (generation == seen) pthread_cond_wait(&ready, &lock);
        seen = generation;
        Decode d = job;
        unsigned parts = (unsigned)workers + 1;
        pthread_mutex_unlock(&lock);
        stripe(&d, part, parts);
        pthread_mutex_lock(&lock);
        if (--pending == 0) pthread_cond_signal(&finished);
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}
static void init_workers(void) {
    int n = SDL_GetNumLogicalCPUCores() - 1;
    const char *e = getenv("SAVAGEWIND_ASTC_WORKERS");
    if (e) n = atoi(e);
    if (n > 3) n = 3;
    for (int i = 0; i < n; i++) {
        pthread_t thread;
        if (pthread_create(&thread, NULL, worker, (void *)(uintptr_t)(i + 1))) break;
        workers++;
        pthread_detach(thread);
    }
}
void vk_astc_decode_ldr(const uint8_t *blocks, uint32_t width, uint32_t height,
                        uint32_t bw, uint32_t bh, bool srgb, uint8_t *out) {
    if (bw < 4 || bh < 4 || bw > 12 || bh > 12 || !width || !height) return;
    if ((uint64_t)width * height < 65536) {
        astc_decode_ldr(blocks, width, height, bw, bh, srgb, out);
        return;
    }
    pthread_once(&once, init_workers);
    // Another caller (e.g. UI) need not wait behind an unrelated image.
    if (!workers || pthread_mutex_trylock(&submit)) {
        astc_decode_ldr(blocks, width, height, bw, bh, srgb, out);
        return;
    }
    Decode d = {blocks, out, width, height, bw, bh, srgb};
    pthread_mutex_lock(&lock);
    job = d;
    pending = workers;
    generation++;
    pthread_cond_broadcast(&ready);
    pthread_mutex_unlock(&lock);
    stripe(&d, 0, (unsigned)workers + 1);
    pthread_mutex_lock(&lock);
    while (pending) pthread_cond_wait(&finished, &lock);
    pthread_mutex_unlock(&lock);
    pthread_mutex_unlock(&submit);
}
