// Standalone native-codec smoke test; no display, audio device, or Orion app.
#import <AVFoundation/AVFoundation.h>
#include <orion/kernel/media_export.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void media_test_u32(uint8_t *p, uint32_t n) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(n >> (8 * i));
}
static bool media_test_cancel(void *userdata, float value) { (void)userdata; (void)value; return false; }
static bool media_test_asset(const char *path, int width) {
  AVURLAsset *asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:path]] options:nil];
  NSArray<AVAssetTrack *> *audio = [asset tracksWithMediaType:AVMediaTypeAudio];
  NSArray<AVAssetTrack *> *video = [asset tracksWithMediaType:AVMediaTypeVideo];
  double duration = CMTimeGetSeconds(asset.duration);
  bool ok = audio.count == 1 && isfinite(duration) && fabs(duration - 1) < .08;
  if (width) ok = ok && video.count == 1 && (int)video.firstObject.naturalSize.width == width && (int)video.firstObject.naturalSize.height == 720;
  else ok = ok && video.count == 0;
  if (!ok) fprintf(stderr, "Native media validation failed: %s audio=%lu video=%lu duration=%g\n", path, (unsigned long)audio.count, (unsigned long)video.count, duration);
  return ok;
}

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  @autoreleasepool {
    char wav[1200], path[1200];
    snprintf(wav, sizeof(wav), "%s/source.wav", argv[1]);
    uint8_t header[44] = {0};
    memcpy(header, "RIFF", 4); media_test_u32(header + 4, 36 + 44100 * 4);
    memcpy(header + 8, "WAVEfmt ", 8); media_test_u32(header + 16, 16); header[20] = 1; header[22] = 2;
    media_test_u32(header + 24, 44100); media_test_u32(header + 28, 44100 * 4); header[32] = 4; header[34] = 16;
    memcpy(header + 36, "data", 4); media_test_u32(header + 40, 44100 * 4);
    FILE *f = fopen(wav, "wb");
    if (!f || fwrite(header, 1, 44, f) != 44) return 1;
    for (int i = 0; i < 44100; i++) {
      int16_t sample = (int16_t)(sin(i * 440.0 * 6.283185307179586 / 44100) * 12000);
      uint8_t pcm[4] = {(uint8_t)sample, (uint8_t)((uint16_t)sample >> 8), (uint8_t)sample, (uint8_t)((uint16_t)sample >> 8)};
      if (fwrite(pcm, 1, 4, f) != 4) return 1;
    }
    if (fclose(f)) return 1;
    snprintf(path, sizeof(path), "%s/audio.m4a", argv[1]);
    if (!ui_media_encode(wav, path, UI_MEDIA_M4A, NULL, NULL, NULL) || !media_test_asset(path, 0)) return 1;
    for (int aspect = 0; aspect < 2; aspect++) {
      int width = aspect ? 1680 : 1280;
      uint8_t *rgba = calloc((size_t)width * 720, 4);
      if (!rgba) return 1;
      for (int i = 0; i < width * 720; i++) { rgba[i * 4] = 32; rgba[i * 4 + 3] = 255; }
      ui_media_video_t video = {.rgba=rgba, .width=width, .height=720, .x=64, .y=92, .w=width-96, .h=596,
        .frames=44100, .rate=44100, .fps=30, .color=0xffffffff};
      snprintf(path, sizeof(path), "%s/timeline-%d.mp4", argv[1], width);
      bool ok = ui_media_encode(wav, path, UI_MEDIA_MP4, &video, NULL, NULL) && media_test_asset(path, width);
      if (!ok) { free(rgba); return 1; }
      snprintf(path, sizeof(path), "%s/cancel-%d.mp4", argv[1], width);
      ok = !ui_media_encode(wav, path, UI_MEDIA_MP4, &video, media_test_cancel, NULL) && access(path, F_OK) != 0;
      free(rgba);
      if (!ok) return 1;
    }
    puts("Native AAC, H.264, 16:9, 21:9, duration and cancellation passed.");
    return 0;
  }
}
