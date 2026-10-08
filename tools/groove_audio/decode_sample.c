#include <stdio.h>
#include <stdlib.h>
#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "apps/groove/minimp3_ex.h"

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "[sample-check] usage: decode_sample input.mp3 output.pcm\n");
    return 1;
  }
  mp3dec_ex_t dec;
  int error = mp3dec_ex_open(&dec, argv[1], MP3D_SEEK_TO_SAMPLE);
  if (error) {
    fprintf(stderr, "[sample-check] open failed file=%s error=%d\n", argv[1], error);
    return 1;
  }
  size_t count = (size_t)dec.samples;
  mp3d_sample_t *pcm = count ? malloc(count * sizeof(*pcm)) : NULL;
  FILE *fp = NULL;
  int status = 1;
  if (!pcm) {
    fprintf(stderr, "[sample-check] empty audio or allocation failed samples=%zu\n", count);
    goto done;
  }
  size_t used = mp3dec_ex_read(&dec, pcm, count);
  if (used != count || dec.last_error) {
    fprintf(stderr, "[sample-check] decode failed samples=%zu/%zu error=%d\n", used, count, dec.last_error);
    goto done;
  }
  fp = fopen(argv[2], "wb");
  if (!fp || fwrite(pcm, sizeof(*pcm), count, fp) != count) {
    fprintf(stderr, "[sample-check] output failed file=%s\n", argv[2]);
    goto done;
  }
  if (fclose(fp)) {
    fp = NULL;
    fprintf(stderr, "[sample-check] close failed file=%s\n", argv[2]);
    goto done;
  }
  fp = NULL;
  printf("%zu %d %d %d\n", count, dec.info.channels, dec.info.hz, dec.start_delay);
  status = 0;
done:
  if (fp) fclose(fp);
  free(pcm);
  mp3dec_ex_close(&dec);
  return status;
}
