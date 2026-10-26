#ifndef ORION_MEDIA_EXPORT_H
#define ORION_MEDIA_EXPORT_H
#include <stdint.h>
#include <stdbool.h>

// Called on the worker thread. Return false to cancel. Values are in [0,1].
typedef bool (*ui_media_progress_fn)(void *userdata, float fraction);
typedef enum { UI_MEDIA_M4A, UI_MEDIA_MP4 } ui_media_format_t;
typedef struct {
  const uint8_t *rgba; // opaque, top-down sRGB8 static timeline, owned by caller
  int width, height;
  int x, y, w, h;     // playhead travel rectangle in output pixels
  uint32_t color;    // packed Orion RGBA
  int64_t frames;    // exact audio duration at `rate`
  int rate, fps;
} ui_media_video_t;
// Encode a PCM WAV on a worker. Apple uses native AVFoundation, other desktops
// use ffmpeg on PATH. Output must be a new staging path, never an existing file.
bool ui_media_encode(const char *wav, const char *output, ui_media_format_t format,
                     const ui_media_video_t *video, ui_media_progress_fn progress, void *userdata);
// Fills a frame with the static timeline and an audio-clocked playhead.
bool ui_media_video_frame(const ui_media_video_t *video, int64_t index, uint8_t *rgba);
// UI thread only. File must remain available until the native share UI closes.
// Returns false where native sharing is unavailable; caller can offer Save As.
bool ui_media_share(const char *path);
#endif
