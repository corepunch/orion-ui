#include <stdbool.h>
#include "audio_input.h"
#include <stdio.h>
#ifndef __APPLE__
ui_microphone_permission_t ui_microphone_permission(bool request) { return UI_MIC_UNAVAILABLE; }
ui_audio_input_t *ui_audio_input_open(int rate, void (*receive)(void *, const int16_t *, int), void *userdata) { fprintf(stderr, "[microphone] capture unavailable on this platform\n"); fflush(stderr); return NULL; }
void ui_audio_input_close(ui_audio_input_t *input) { }
#endif
