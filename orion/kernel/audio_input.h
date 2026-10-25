#ifndef ORION_AUDIO_INPUT_H
#define ORION_AUDIO_INPUT_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct ui_audio_input ui_audio_input_t;
typedef enum { UI_MIC_UNKNOWN, UI_MIC_PENDING, UI_MIC_ALLOWED, UI_MIC_DENIED, UI_MIC_UNAVAILABLE } ui_microphone_permission_t;
// Request access only in response to a user action. Poll again on the UI thread.
ui_microphone_permission_t ui_microphone_permission(bool request);
// Mono signed 16-bit PCM. The callback must not allocate, block, or call UI APIs.
ui_audio_input_t *ui_audio_input_open(int rate, void (*receive)(void *, const int16_t *, int), void *userdata);
// Stops synchronously: no callback can run after close returns.
void ui_audio_input_close(ui_audio_input_t *input);
#endif
