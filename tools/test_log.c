// Windows: clang -O2 -Iruntime tools/test_log.c -o build/test_log.exe
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include "rt.h"
int main(void) {
    char message[8193];
    memset(message, 'x', sizeof message - 1);
    message[sizeof message - 1] = 0;
    if (!freopen("build/test_log-output.txt", "wb+", stderr)) return 1;
    setvbuf(stderr, NULL, _IONBF, 0);
    LARGE_INTEGER frequency, a, b, c;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&a);
    for (int i = 0; i < 30; i++) fprintf(stderr, "%.1000s\n", message);
    QueryPerformanceCounter(&b);
    for (int i = 0; i < 30; i++) rt_log("%.1000s\n", message);
    QueryPerformanceCounter(&c);
    long offset = ftell(stderr);
    rt_log("%s:%d:%s\n", "中文", 42, message);
    char expected[9000], actual[9000];
    int size = snprintf(expected, sizeof expected, "%s:%d:%s\n", "中文", 42, message);
    if (size >= 8192) size = 8191;  // upstream bounds each record to its stack buffer
    if (ftell(stderr) - offset != size || fseek(stderr, offset, SEEK_SET)) return 2;
    if (fread(actual, 1, (size_t)size, stderr) != (size_t)size || memcmp(expected, actual, (size_t)size)) return 3;
    printf("PASS: upstream bounded UTF-8 log record; mean old %.3f ms, new %.3f ms\n",
           (b.QuadPart - a.QuadPart) * 1000.0 / frequency.QuadPart / 30,
           (c.QuadPart - b.QuadPart) * 1000.0 / frequency.QuadPart / 30);
    return 0;
}
