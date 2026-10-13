#define IMAGEEDITOR_BW 1
#define IMAGEEDITOR_BW_RETINA
#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"
#include "apps/imageeditor/io/flc_codec.h"
#include "anim_test_helpers.h"

app_state_t *g_app;
int g_bw_retina_scale = 1;
static bool flc_decode(uint16_t type, const uint8_t *p, size_t size, uint8_t *pixels, int w, int h, uint32_t pal[256]);

static bool rle_check(const uint8_t *pixels, const uint8_t *old, int w, int h, int expected) {
  size_t size, n = (size_t)w * h;
  uint8_t *chunk = flc_codec_encode(pixels, old, w, h, &size), *decoded = calloc(1, n), *reference = calloc(1, n);
  bool ok = chunk && decoded && reference;
  if (ok && old) memcpy(reference, old, n);
  if (ok) ok = size <= 6 + n + (n & 1) &&
               (expected < 0 || flc_codec_u16(chunk + 4) == expected) &&
               flc_codec_decode(chunk, size, decoded, old, w, h) && !memcmp(decoded, pixels, n) &&
               flc_decode(flc_codec_u16(chunk + 4), chunk + 6, size - 6, reference, w, h, NULL) && !memcmp(reference, pixels, n);
  free(chunk); free(decoded); free(reference);
  return ok;
}

static void rle_runs(void) {
  TEST("BRUN matches FLIC decoding across run boundaries, odd widths and over 255 packets");
  const int widths[] = {1,2,3,126,127,128,129,255,256,257,16384};
  uint8_t pixels[16384 * 3];
  for (int i = 0; i < ARRAY_LEN(widths); i++) {
    int w = widths[i];
    memset(pixels, 255, (size_t)w * 3);
    ASSERT_TRUE(rle_check(pixels, NULL, w, 3, w > 3 ? 15 : 16));
    for (int p = 0; p < w * 3; p++) pixels[p] = (p / 3) % 2 + 1;
    ASSERT_TRUE(rle_check(pixels, NULL, w, 3, -1));
  }
  memset(pixels, 0, sizeof(pixels));
  ASSERT_TRUE(rle_check(pixels, NULL, 16384, 3, 13));
  for (int p = 0; p < 2001; p++) pixels[p] = p / 3 % 2 + 1;
  size_t size; uint8_t *chunk = flc_codec_encode(pixels, NULL, 2001, 1, &size);
  ASSERT_NOT_NULL(chunk); ASSERT_EQUAL(flc_codec_u16(chunk + 4), 15); ASSERT_EQUAL(chunk[6], 0);
  free(chunk); PASS();
}

static void rle_deltas(void) {
  TEST("LC preserves unchanged rows, long column skips, repeated cels and opposite packet signs");
  uint8_t old[1025 * 5], pixels[sizeof(old)];
  memset(old, 255, sizeof(old)); memcpy(pixels, old, sizeof(old));
  pixels[2 * 1025 + 800] = 17; pixels[3 * 1025 - 1] = 42;
  ASSERT_TRUE(rle_check(pixels, old, 1025, 5, 12));
  ASSERT_TRUE(rle_check(pixels, pixels, 1025, 5, 12));
  memset(pixels + 2 * 1025 + 300, 7, 128);
  ASSERT_TRUE(rle_check(pixels, old, 1025, 5, 12));
  size_t size; uint8_t *chunk = flc_codec_encode(pixels, old, 1025, 5, &size);
  ASSERT_NOT_NULL(chunk);
  uint8_t decoded[sizeof(old)];
  ASSERT_FALSE(flc_codec_decode(chunk, size, decoded, NULL, 1025, 5));
  free(chunk); PASS();
}

static void rle_noise(void) {
  TEST("random and mixed temporal sequences remain byte-exact and bounded by COPY size");
  uint8_t old[257 * 7], pixels[sizeof(old)];
  uint32_t seed = 0x12345678;
  memset(old, 255, sizeof(old));
  for (int frame = 0; frame < 240; frame++) {
    memcpy(pixels, old, sizeof(old));
    for (size_t p = 0; p < sizeof(old); p++) {
      seed = seed * 1664525u + 1013904223u;
      if (frame % 4 == 0 || (seed & 31) == 0) pixels[p] = seed >> 24;
    }
    if (frame % 4 == 1) memset(pixels + 257, 42, 257);
    if (frame % 4 == 3) memcpy(pixels, old, sizeof(old));
    ASSERT_TRUE(rle_check(pixels, frame ? old : NULL, 257, 7, frame == 0 ? 16 : -1));
    memcpy(old, pixels, sizeof(old));
  }
  PASS();
}

static void rle_reject(void) {
  TEST("malformed RLE cannot cross rows, overrun payloads, omit a delta base or hide trailing data");
  uint8_t pixels[16] = {0}, old[16] = {0};
  uint8_t brun[] = {10,0,0,0, 15,0, 1,4,7,0};
  ASSERT_TRUE(flc_codec_decode(brun, sizeof(brun), pixels, NULL, 4, 1));
  brun[7] = 5; ASSERT_FALSE(flc_codec_decode(brun, sizeof(brun), pixels, NULL, 4, 1));
  brun[7] = 0; ASSERT_FALSE(flc_codec_decode(brun, sizeof(brun), pixels, NULL, 4, 1));
  brun[7] = 0xfc; ASSERT_FALSE(flc_codec_decode(brun, sizeof(brun), pixels, NULL, 4, 1));
  brun[7] = 4; brun[9] = 1; ASSERT_FALSE(flc_codec_decode(brun, sizeof(brun), pixels, NULL, 4, 1));
  brun[9] = 0;
  for (size_t cut = 0; cut < sizeof(brun); cut++) ASSERT_FALSE(flc_codec_decode(brun, cut, pixels, NULL, 4, 1));
  uint8_t lc[] = {14,0,0,0, 12,0, 0,0,1,0, 1,0,0xff,7};
  ASSERT_TRUE(flc_codec_decode(lc, sizeof(lc), pixels, old, 4, 1));
  ASSERT_FALSE(flc_codec_decode(lc, sizeof(lc), pixels, NULL, 4, 1));
  lc[8] = 2; ASSERT_FALSE(flc_codec_decode(lc, sizeof(lc), pixels, old, 4, 1));
  lc[8] = 1; lc[11] = 4; ASSERT_FALSE(flc_codec_decode(lc, sizeof(lc), pixels, old, 4, 1));
  PASS();
}

static void rle_native(void) {
  TEST("app saves compact editable RLE projects; old layer versions and corrupt records are rejected");
  char path[] = "/tmp/orion-rle-XXXXXX";
  int fd = mkstemp(path); ASSERT_TRUE(fd >= 0); close(fd);
  test_env_init(); g_app = calloc(1, sizeof(*g_app)); ASSERT_NOT_NULL(g_app);
  canvas_doc_t *doc = create_document(NULL, 1025, 5); ASSERT_NOT_NULL(doc);
  memset(doc->pixels, 123, 1025 * 5);
  cmd_frame_add(doc, true); doc->pixels[2 * 1025 + 800] = 201;
  ASSERT_TRUE(flc_save(path, doc));
  FILE *fp = fopen(path, "rb"); ASSERT_NOT_NULL(fp);
  uint8_t header[128]; ASSERT_EQUAL(fread(header, 1, 128, fp), 128);
  ASSERT_TRUE(flc_codec_u32(header) < 4096);
  ASSERT_EQUAL(flc_codec_u32(header + 80), 128);
  ASSERT_TRUE(flc_codec_u32(header + 84) > 128);
  long layers = 0;
  while (!layers) {
    long pos = ftell(fp); uint8_t ch[16]; ASSERT_EQUAL(fread(ch, 1, 16, fp), 16);
    if (flc_codec_u16(ch + 4) == 0x7075) { layers = pos; break; }
    ASSERT_EQUAL(fseek(fp, flc_codec_u32(ch) - 16, SEEK_CUR), 0);
  }
  fclose(fp);
  int w, h; uint32_t palette[256], bg; bool show;
  anim_timeline_t *tl = flc_load(path, &w, &h, palette, &bg, &show);
  ASSERT_NOT_NULL(tl); ASSERT_EQUAL(test_anim_cel_pixel(tl->frames[0], 1, 0, 1025, 5), 123);
  ASSERT_EQUAL(test_anim_cel_pixel(tl->frames[1], 1, 2 * 1025 + 800, 1025, 5), 201);
  anim_timeline_free(tl);
  fp = fopen(path, "r+b"); ASSERT_NOT_NULL(fp);
  ASSERT_EQUAL(fseek(fp, layers + 11, SEEK_SET), 0); fputc('4', fp); fclose(fp);
  ASSERT_NULL(flc_load(path, &w, &h, palette, &bg, &show));
  fp = fopen(path, "r+b"); ASSERT_NOT_NULL(fp);
  ASSERT_EQUAL(fseek(fp, layers + 11, SEEK_SET), 0); fputc('5', fp);
  ASSERT_EQUAL(fseek(fp, layers + 16, SEEK_SET), 0); fputc(0xff, fp); fclose(fp);
  ASSERT_NULL(flc_load(path, &w, &h, palette, &bg, &show));
  close_document(doc); free(g_app); g_app = NULL; test_env_shutdown(); unlink(path);
  PASS();
}

int main(void) {
  TEST_START("Penciltest lossless RLE");
  rle_runs(); rle_deltas(); rle_noise(); rle_reject(); rle_native();
  TEST_END();
}
