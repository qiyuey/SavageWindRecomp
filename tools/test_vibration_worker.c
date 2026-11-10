#define VIBRATION_WORKER_TEST
#include <stdatomic.h>
#define main vibration_contract_main
#include "test_vibration.c"
#undef main

static void pause_ms(unsigned ms) {
#ifdef _WIN32
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, 0x2, TIMER_ALL_ACCESS);
    assert(timer);
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)ms * 10000;
    assert(SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE));
    WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
#else
    struct timespec delay = {ms / 1000, (long)(ms % 1000) * 1000000};
    nanosleep(&delay, NULL);
#endif
}
static void wait_calls(unsigned minimum) {
    uint64_t deadline = SDL_GetTicksNS() + 500000000;
    for (;;) {
        pthread_mutex_lock(&lock);
        bool done = calls >= minimum && have_last && !retry &&
                    memcmp(last, bands, sizeof last) == 0;
        pthread_mutex_unlock(&lock);
        if (done) return;
        if (SDL_GetTicksNS() >= deadline) {
            pthread_mutex_lock(&lock);
            fprintf(stderr, "timeout waiting %u calls: got %u, retry %d stop %d accepting %d focus %d running %d\n",
                    minimum, calls, retry, stop_pending, accepting, focused_snapshot, running);
            pthread_mutex_unlock(&lock);
            abort();
        }
        pause_ms(1);
    }
}
static void *batch_producer(void *arg) {
    atomic_bool *done = arg;
    uint32_t handles[] = {3, 0x10003};
    for (unsigned i = 1; i <= 10000; ++i) {
        float a = (i % 100 + 1) / 200.0f;
        VibrationValue pair[] = {{a, 160, 0, 320}, {a, 160, 0, 320}};
        vibration_send_batch(handles, pair, 2);
        if (i % 500 == 0) pause_ms(1);
    }
    atomic_store(done, true);
    return NULL;
}
int main(void) {
    SDL_Gamepad *pad = (SDL_Gamepad *)(uintptr_t)1;
    vibration_update(pad, true);
    wait_calls(1);
    // Keep the event thread idle: the worker must still submit changing state.
    uint32_t handles[] = {3, 0x10003};
    unsigned first;
    pthread_mutex_lock(&lock); first = calls; pthread_mutex_unlock(&lock);
    for (unsigned i = 0; i < 60; ++i) {
        VibrationValue pair[] = {{.1f + i * .001f, 160, .2f, 320}, {.1f + i * .001f, 160, .2f, 320}};
        vibration_send_batch(handles, pair, 2);
        pause_ms(1);
    }
    pthread_mutex_lock(&lock);
    unsigned end = calls;
    assert(end - first >= 10);
    assert(joystick_updates >= end); // A 16 ms event-loop-only path cannot pass this idle test.
    double sum = 0, minimum = 1e9, maximum = 0;
    for (unsigned i = first + 1; i < end; ++i) {
        double ms = (call_times[i] - call_times[i - 1]) / 1e6;
        sum += ms;
        if (ms < minimum) minimum = ms;
        if (ms > maximum) maximum = ms;
    }
    assert(sum / (end - first - 1) < 4.0); // A healthy publisher must not wait for a 4 ms send tick.
    printf("worker: %u active intervals, mean %.3f ms, min %.3f ms, max %.3f ms (mock SDL, idle UI)\n",
           end - first - 1, sum / (end - first - 1), minimum, maximum);
    pthread_mutex_unlock(&lock);

    // Stereo publication remains coherent under concurrent reading and writes.
    vibration_update(pad, true);
    pthread_mutex_lock(&lock); check_stereo = true; pthread_mutex_unlock(&lock);
    pthread_t producer;
    atomic_bool producer_done = false;
    assert(pthread_create(&producer, NULL, batch_producer, &producer_done) == 0);
    for (unsigned i = 0; i < 10000 || !atomic_load(&producer_done); ++i) {
        pthread_mutex_lock(&lock);
        assert(values[0].low_amplitude == values[1].low_amplitude);
        pthread_mutex_unlock(&lock);
        if (i % 500 == 0) vibration_update(pad, true);
    }
    pthread_join(producer, NULL);
    // Wait for the observed output, not an assumed Windows scheduling delay.
    // This phase models a live event thread, so keep its heartbeat fresh.
    uint64_t stereo_deadline = SDL_GetTicksNS() + 500000000;
    for (;;) {
        pthread_mutex_lock(&lock);
        bool delivered = last[0].low_amplitude == .005f && last[1].low_amplitude == .005f;
        pthread_mutex_unlock(&lock);
        if (delivered) break;
        assert(SDL_GetTicksNS() < stereo_deadline);
        vibration_update(pad, true);
        pause_ms(1);
    }
    pthread_mutex_lock(&lock);
    assert(last[0].low_amplitude == .005f && last[1].low_amplitude == .005f);
    check_stereo = false;
    pthread_mutex_unlock(&lock);

    // Park SDL inside an output call: publication must finish before SDL is released.
    vibration_update(pad, true);
    pthread_mutex_lock(&lock);
    stall_output = true;
    output_entered = false;
    unsigned stalled_before = calls;
    pthread_mutex_unlock(&lock);
    VibrationValue stalled = {.6f, 160, .1f, 320};
    vibration_send(3, &stalled);
    uint64_t deadline = SDL_GetTicksNS() + 500000000;
    for (;;) {
        pthread_mutex_lock(&lock); bool entered = output_entered; pthread_mutex_unlock(&lock);
        if (entered) break;
        assert(SDL_GetTicksNS() < deadline);
        pause_ms(1);
    }
    vibration_clear();
    vibration_send(3, &stalled);
    pthread_mutex_lock(&lock);
    assert(stall_output && stop_pending);
    stall_output = false;
    pthread_cond_signal(&test_output_gate);
    pthread_mutex_unlock(&lock);
    wait_calls(stalled_before + 3);
    pthread_mutex_lock(&lock); assert(last[0].low_amplitude == .6f); pthread_mutex_unlock(&lock);

    // Fail the next two changed submissions; unchanged target must be retried.
    vibration_update(pad, true);
    pthread_mutex_lock(&lock); failures = 2; unsigned before = calls; pthread_mutex_unlock(&lock);
    VibrationValue v = {.8f, 180, .1f, 400};
    vibration_send(3, &v);
    wait_calls(before + 3);
    pthread_mutex_lock(&lock);
    assert(!retry && last[0].low_frequency == 180);
    pthread_mutex_unlock(&lock);

    // A stop followed immediately by a new effect must not be overwritten.
    vibration_update(pad, true);
    pthread_mutex_lock(&lock);
    before = calls;
    neutral_locked();
    store_locked(3, &v);
    publish_locked();
    assert(stop_pending);
    pthread_mutex_unlock(&lock);
    wait_calls(before + 2);
    pthread_mutex_lock(&lock); assert(last[0].low_amplitude == .8f); pthread_mutex_unlock(&lock);

    // Complete silence stops both sides; no stale active output resumes.
    v = (VibrationValue){0, 160, 0, 320};
    vibration_send_batch(handles, (VibrationValue[]){v, v}, 2);
    pause_ms(10);
    pthread_mutex_lock(&lock); assert(duration == 0); pthread_mutex_unlock(&lock);
    vibration_update(pad, true);
    v.low_amplitude = .5f;
    vibration_send(3, &v);
    pause_ms(8);
    vibration_update(pad, false);
    pause_ms(8);
    pthread_mutex_lock(&lock); assert(duration == 0); pthread_mutex_unlock(&lock);
    vibration_update(pad, true);
    pause_ms(8);
    pthread_mutex_lock(&lock); assert(duration == 0); pthread_mutex_unlock(&lock);

    // A failed stop remains pending and is retried, then mute gates new output.
    vibration_send(3, &v);
    pause_ms(8);
    pthread_mutex_lock(&lock); failures = 2; before = calls; pthread_mutex_unlock(&lock);
    vibration_permit(false);
    wait_calls(before + 3);
    pthread_mutex_lock(&lock); assert(duration == 0 && !retry); pthread_mutex_unlock(&lock);
    vibration_permit(true);
    vibration_send(3, &v);
    pause_ms(8);
    g_settings.rumble_strength = 0;
    vibration_update(pad, true);
    pause_ms(8);
    pthread_mutex_lock(&lock); assert(duration == 0); pthread_mutex_unlock(&lock);
    g_settings.rumble_strength = 20;
    vibration_update(pad, true);

    // Event-thread stall expires the heartbeat even if the worker is healthy.
    vibration_send(3, &v);
    pause_ms(130);
    pthread_mutex_lock(&lock); assert(duration == 0 && !accepting); pthread_mutex_unlock(&lock);
    vibration_update(pad, true);
    vibration_send(3, &v);
    pause_ms(8);
    vibration_reset();
    pause_ms(8);
    pthread_mutex_lock(&lock); assert(duration == 0); pthread_mutex_unlock(&lock);

    // Detach serializes with output. There must be no call after the close point.
    vibration_send(3, &v);
    vibration_detach(pad);
    pthread_mutex_lock(&lock); before = calls; pthread_mutex_unlock(&lock);
    pause_ms(12);
    pthread_mutex_lock(&lock); assert(calls == before && output_pad == NULL); pthread_mutex_unlock(&lock);
    vibration_shutdown();
    vibration_update(pad, true);
    pause_ms(8);
    pthread_mutex_lock(&lock); assert(duration == 0); pthread_mutex_unlock(&lock);
    vibration_detach(pad);
    vibration_shutdown();
    vibration_update(pad, true);
    vibration_exit();
    vibration_update(pad, true);
    pthread_mutex_lock(&lock);
    assert(!started && output_pad == NULL);
    pthread_mutex_unlock(&lock);
    puts("worker: stereo batch, retries, stop, focus, heartbeat, reset, detach and restart passed");
    return 0;
}
