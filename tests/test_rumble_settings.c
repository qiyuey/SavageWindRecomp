// Run twice with the same temporary file to verify persistence across separate boots.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../runtime/settings.c"
#include "../runtime/hle/bindings.c"

const TextLanguage g_text_languages[] = {{"Auto", NULL, NULL}};
const VoiceLanguage g_voice_languages[] = {{"Auto", NULL}};
const int g_ntext_languages = 1, g_nvoice_languages = 1;
int input_pad_family(void) { return 0; }
bool fsr3_upscaler_enabled(void) { return false; }
float winsize_refresh_rate(void) { return 0; }

int main(int argc, char **argv) {
    assert(argc == 5);
    setenv("SAVAGEWIND_SETTINGS", argv[1], 1);
    settings_load();
    assert(g_settings.rumble_strength == atoi(argv[2]));
    assert(g_settings.rumble == (atoi(argv[4]) != 0));
    g_settings.rumble_strength = atoi(argv[3]);
    settings_save();
    puts("PASS: rumble strength default, bounds and persistence");
}
