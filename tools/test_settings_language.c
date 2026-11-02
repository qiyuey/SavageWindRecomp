// Each invocation is a fresh process: settings_load intentionally runs only once.
#include "settings.h"
#include "language.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool bind_settings_load(const char *key, const char *val) { (void)key; (void)val; return false; }
void bind_settings_save(FILE *f) { (void)f; }
// This language-only harness does not initialize a renderer or upscaler.
bool fsr3_upscaler_enabled(void) { return false; }
int rt_log(const char *format, ...) { (void)format; return 0; }
const char *rt_data_root(void) { return "."; }

int main(int argc, char **argv) {
    assert(argc == 3);
    const char *path = getenv("SAVAGEWIND_SETTINGS");
    assert(path && strstr(path, "build/"));
    if (strcmp(argv[1], "missing")) {
        FILE *f = fopen(path, "w"); assert(f);
        fputs("settings_version = 3\n", f);
        if (strcmp(argv[1], "legacy")) fprintf(f, "text_language = %s\n", argv[1]);
        fclose(f);
    } else remove(path);
    settings_load();
    if (!strcmp(argv[2], "auto")) assert(g_settings.text_language == 0);
    else assert(!strcmp(language_text_code(), argv[2]));
    settings_save();
    FILE *f = fopen(path, "r"); assert(f);
    char text[4096]; size_t n = fread(text, 1, sizeof text - 1, f); text[n] = 0; fclose(f);
    const char *folder = g_settings.text_language ? g_text_languages[g_settings.text_language].folder : "auto";
    char expected[64]; snprintf(expected, sizeof expected, "text_language = %s\n", folder);
    assert(strstr(text, expected));
    printf("PASS: %s -> %s (saved %s)\n", argv[1], argv[2], folder);
}
