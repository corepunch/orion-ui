#include "test_framework.h"
#include "apps/groove/groove.h"

static block_t cache_blocks[2];
static int uploads, deletes, uploaded_w, uploaded_h;
static bool fail_upload;
static uint8_t *uploaded_mask;

const block_t *block_get(int id) { return id >= 0 && id < 2 ? &cache_blocks[id] : NULL; }
static uint32_t cache_test_upload(int w, int h, const void *pixels, R_TextureFilter filter, R_TextureWrap wrap) {
  if (fail_upload || filter != R_FILTER_NEAREST || wrap != R_WRAP_CLAMP) return 0;
  free(uploaded_mask);
  uploaded_mask = malloc((size_t)w * h);
  if (!uploaded_mask) return 0;
  memcpy(uploaded_mask, pixels, (size_t)w * h);
  uploaded_w = w; uploaded_h = h;
  return ++uploads;
}
static void cache_test_delete(uint32_t texture) { if (texture) deletes++; }

#define R_CreateTextureR8 cache_test_upload
#define R_DeleteTexture cache_test_delete
#include "apps/groove/waveform_cache.c"
#undef R_CreateTextureR8
#undef R_DeleteTexture

static void test_shared_cache(void) {
  TEST("repeated card paints share waveform uploads; changed audio, size and silhouette invalidate the cache");
  groove_t app = {0};
  cache_blocks[0] = (block_t){ .audio = {.npeaks = 2, .peaks = {255, 255}}, .audio_revision = 1 };
  cache_blocks[1] = cache_blocks[0];
  g_ui_runtime.running = true;
  uploads = deletes = 0;
  uint32_t first = waveform_texture(&app, 0, (ipoint16_t){20, 32}, 0);
  ASSERT_NOT_EQUAL(first, 0);
  for (int i = 0; i < 100; i++) ASSERT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){20, 32}, 0), first);
  ASSERT_EQUAL(uploads, 1);
  ASSERT_EQUAL(deletes, 0);
  ASSERT_EQUAL(uploaded_mask[13 * uploaded_w + 10], 0);
  ASSERT_EQUAL(uploaded_mask[14 * uploaded_w + 10], 255);
  ASSERT_EQUAL(uploaded_mask[(uploaded_h - 1) * uploaded_w + 10], 255);
  ASSERT_NOT_EQUAL(waveform_texture(&app, 1, (ipoint16_t){20, 32}, 0), first);
  ASSERT_EQUAL(uploads, 2);
  cache_blocks[0].audio_revision++;
  memset(cache_blocks[0].audio.peaks, 0, sizeof(cache_blocks[0].audio.peaks));
  ASSERT_NOT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){20, 32}, 0), first);
  ASSERT_EQUAL(uploads, 3);
  ASSERT_EQUAL(deletes, 1);
  ASSERT_EQUAL(uploaded_mask[14 * uploaded_w + 10], 0);
  ASSERT_EQUAL(uploaded_mask[(uploaded_h - 1) * uploaded_w + 10], 255);
  ASSERT_NOT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){21, 33}, 0), 0);
  ASSERT_EQUAL(uploaded_w, 21);
  ASSERT_EQUAL(uploaded_h, 33);
  ASSERT_EQUAL(uploads, 4);
  ASSERT_NOT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){21, 33}, 8), 0);
  ASSERT_EQUAL(uploads, 5);
  ASSERT_EQUAL(uploaded_mask[(uploaded_h - 1) * uploaded_w], 0);
  ASSERT_EQUAL(uploaded_mask[(uploaded_h - 1) * uploaded_w + 10], 255);
  waveform_cache_free(&app);
  ASSERT_EQUAL(deletes, uploads);
  ASSERT_EQUAL(app.waveforms[0].texture, 0);
  waveform_cache_free(&app);
  ASSERT_EQUAL(deletes, uploads);
  g_ui_runtime.running = false;
  PASS();
}

static void test_failed_refresh(void) {
  TEST("failed uploads preserve cache ownership and retry after recovery; empty or headless waveforms do not upload");
  groove_t app = {0};
  uploads = deletes = 0;
  g_ui_runtime.running = false;
  ASSERT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){20, 32}, 0), 0);
  ASSERT_EQUAL(uploads, 0);
  g_ui_runtime.running = true;
  uint32_t first = waveform_texture(&app, 0, (ipoint16_t){20, 32}, 0);
  ASSERT_NOT_EQUAL(first, 0);
  cache_blocks[0].audio_revision++;
  fail_upload = true;
  ASSERT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){20, 32}, 0), 0);
  ASSERT_EQUAL(app.waveforms[0].texture, first);
  ASSERT_EQUAL(deletes, 0);
  fail_upload = false;
  ASSERT_NOT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){20, 32}, 0), first);
  ASSERT_EQUAL(uploads, 2);
  ASSERT_EQUAL(deletes, 1);
  cache_blocks[0].audio.npeaks = 0;
  ASSERT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){20, 32}, 0), 0);
  ASSERT_EQUAL(uploads, 2);
  waveform_cache_free(&app);
  ASSERT_EQUAL(deletes, uploads);
  g_ui_runtime.running = false;
  PASS();
}

int main(void) {
  TEST_START("Groove waveform cache");
  test_shared_cache();
  test_failed_refresh();
  free(uploaded_mask);
  TEST_END();
}
