#include "test_framework.h"
#include "apps/groove/groove.h"

static block_t cache_blocks[2];
static int uploads, deletes, uploaded_w, uploaded_h;
static bool fail_upload;
static uint8_t *uploaded_mask;
static uint8_t drawn_mask[512 * 32];

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
static void cache_test_fill(uint32_t color, irect16_t r) {
  for (int y = r.y; y < r.y + r.h; y++)
    for (int x = r.x; x < r.x + r.w; x++)
      if (x >= 0 && x < 512 && y >= 0 && y < 32) drawn_mask[y * 512 + x] = 255;
}

#define R_CreateTextureR8 cache_test_upload
#define R_DeleteTexture cache_test_delete
#define fill_rect cache_test_fill
#include "apps/groove/waveform_cache.c"
#undef R_CreateTextureR8
#undef R_DeleteTexture
#undef fill_rect

static void test_shared_cache(void) {
  TEST("repeated card paints share waveform uploads; changed audio, size and silhouette invalidate the cache");
  groove_t app = {0};
  cache_blocks[0] = (block_t){ .audio = {.npeaks = 2, .peaks = {255, 255}}, .audio_revision = 1 };
  cache_blocks[1] = cache_blocks[0];
  g_ui_runtime.running = true;
  uploads = deletes = 0;
  uint32_t first = waveform_texture(&app, 0, (ipoint16_t){20, 32}, 0, 20, 0);
  ASSERT_NOT_EQUAL(first, 0);
  for (int i = 0; i < 100; i++) ASSERT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){20, 32}, 0, 20, 0), first);
  ASSERT_EQUAL(uploads, 1);
  ASSERT_EQUAL(deletes, 0);
  ASSERT_EQUAL(uploaded_mask[10], 255);
  ASSERT_EQUAL(uploaded_mask[14 * uploaded_w + 10], 255);
  ASSERT_EQUAL(uploaded_mask[(uploaded_h - 1) * uploaded_w + 10], 255);
  ASSERT_NOT_EQUAL(waveform_texture(&app, 1, (ipoint16_t){20, 32}, 0, 20, 0), first);
  ASSERT_EQUAL(uploads, 2);
  cache_blocks[0].audio_revision++;
  memset(cache_blocks[0].audio.peaks, 0, sizeof(cache_blocks[0].audio.peaks));
  ASSERT_NOT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){20, 32}, 0, 20, 0), first);
  ASSERT_EQUAL(uploads, 3);
  ASSERT_EQUAL(deletes, 1);
  ASSERT_EQUAL(uploaded_mask[14 * uploaded_w + 10], 0);
  ASSERT_EQUAL(uploaded_mask[15 * uploaded_w + 10], GROOVE_WAVEFORM_CENTERED ? 255 : 0);
  ASSERT_EQUAL(uploaded_mask[16 * uploaded_w + 10], GROOVE_WAVEFORM_CENTERED ? 255 : 0);
  ASSERT_EQUAL(uploaded_mask[(uploaded_h - 1) * uploaded_w + 10], GROOVE_WAVEFORM_CENTERED ? 0 : 255);
  ASSERT_NOT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){21, 33}, 0, 21, 0), 0);
  ASSERT_EQUAL(uploaded_w, 21);
  ASSERT_EQUAL(uploaded_h, 33);
  ASSERT_EQUAL(uploads, 4);
  ASSERT_NOT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){21, 33}, 8, 21, 0), 0);
  ASSERT_EQUAL(uploads, 5);
  ASSERT_EQUAL(uploaded_mask[(uploaded_h - 1) * uploaded_w], 0);
  ASSERT_EQUAL(uploaded_mask[(GROOVE_WAVEFORM_CENTERED ? uploaded_h / 2 : uploaded_h - 1) * uploaded_w + 10], 255);
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
  ASSERT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){20, 32}, 0, 20, 0), 0);
  ASSERT_EQUAL(uploads, 0);
  g_ui_runtime.running = true;
  uint32_t first = waveform_texture(&app, 0, (ipoint16_t){20, 32}, 0, 20, 0);
  ASSERT_NOT_EQUAL(first, 0);
  cache_blocks[0].audio_revision++;
  fail_upload = true;
  ASSERT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){20, 32}, 0, 20, 0), 0);
  ASSERT_EQUAL(app.waveforms[0].texture, first);
  ASSERT_EQUAL(deletes, 0);
  fail_upload = false;
  ASSERT_NOT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){20, 32}, 0, 20, 0), first);
  ASSERT_EQUAL(uploads, 2);
  ASSERT_EQUAL(deletes, 1);
  cache_blocks[0].audio.npeaks = 0;
  ASSERT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){20, 32}, 0, 20, 0), 0);
  ASSERT_EQUAL(uploads, 2);
  waveform_cache_free(&app);
  ASSERT_EQUAL(deletes, uploads);
  g_ui_runtime.running = false;
  PASS();
}

static void test_grid_viewport(void) {
  TEST("card margins crop a grid-aligned waveform; trimming and looping preserve beat positions in both painters");
  groove_t app = {0};
  cache_blocks[0] = (block_t){ .audio = {.npeaks = 88}, .audio_revision = 1 };
  for (int beat = 0; beat < 4; beat++) cache_blocks[0].audio.peaks[beat * 22] = 255;
  g_ui_runtime.running = true;
  ASSERT_NOT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){88, 32}, 0, 88, 0), 0);
  uint8_t reference[88 * 32];
  memcpy(reference, uploaded_mask, sizeof(reference));
  const int widths[] = {40, 84, 172, 260};
  for (int i = 0; i < ARRAY_LEN(widths); i++) {
    int width = widths[i];
    ASSERT_NOT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){width, 32}, 0, 88, 2), 0);
    memset(drawn_mask, 0, sizeof(drawn_mask));
    draw_waveform(&cache_blocks[0], R(2, 0, width, 32), 88, 2, 0, 0xFFFFFFFFu);
    for (int y = 0; y < 32; y++) {
      ASSERT_EQUAL(drawn_mask[y * 512 + 1], 0);
      ASSERT_EQUAL(drawn_mask[y * 512 + width + 2], 0);
      for (int x = 0; x < width; x++) {
        uint8_t expected = reference[y * 88 + (x + 2) % 88];
        ASSERT_EQUAL(uploaded_mask[y * width + x], expected);
        ASSERT_EQUAL(drawn_mask[y * 512 + x + 2], expected);
      }
    }
    ASSERT_EQUAL(uploaded_mask[14 * width + 20], 255); // beat at x=22, regardless of viewport width
    ASSERT_EQUAL(uploaded_mask[14 * width + 22], 0);
  }
  uint32_t last = app.waveforms[0].texture;
  ASSERT_NOT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){260, 32}, 0, 176, 2), last);
  last = app.waveforms[0].texture;
  ASSERT_NOT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){260, 32}, 0, 176, 1), last);
  waveform_cache_free(&app);
  g_ui_runtime.running = false;
  PASS();
}

static void test_card_margin(void) {
  TEST("plastic card content uses the theme's halved shadow margin and keeps its grid bounds");
  for (int theme = THEME_CLASSIC; theme <= THEME_MODERN; theme++) {
    set_theme(theme);
    ASSERT_EQUAL(get_theme()->card.shadow_size, 1);
    irect16_t bounds = R(0, 0, 88, 32);
    irect16_t content = theme_content_rect(THEME_PART_CARD, bounds, CTRL_PLASTIC);
    ASSERT_EQUAL(content.x, get_theme()->card.shadow_size + get_theme()->card.ring_width);
    ASSERT_EQUAL(content.w + 2 * content.x, bounds.w);
    irect16_t selected = theme_content_rect(THEME_PART_CARD, bounds, CTRL_PLASTIC | CTRL_SELECTED);
    ASSERT_EQUAL(selected.x, content.x);
    ASSERT_EQUAL(selected.w, content.w);
  }
  PASS();
}

static void test_waveform_alignment(void) {
  TEST("cached and repeated waveforms share alignment and rounded clipping at odd, even and minimal heights");
  groove_t app = {0};
  cache_blocks[0] = (block_t){ .audio = {.npeaks = 4, .peaks = {0, 64, 128, 255}}, .audio_revision = 1 };
  g_ui_runtime.running = true;
  const int heights[] = {1, 2, 17, 31, 32};
  for (int i = 0; i < ARRAY_LEN(heights); i++) {
    int height = heights[i];
    for (int radius = 0; radius <= 8; radius += 8) {
      ASSERT_NOT_EQUAL(waveform_texture(&app, 0, (ipoint16_t){20, height}, radius, 20, 0), 0);
      memset(drawn_mask, 0, sizeof(drawn_mask));
      draw_waveform(&cache_blocks[0], R(0, 0, 20, height), 20, 0, radius, 0xFFFFFFFFu);
      for (int y = 0; y < height; y++) {
        for (int x = 0; x < 20; x++) {
          ASSERT_EQUAL(drawn_mask[y * 512 + x], uploaded_mask[y * 20 + x]);
#if GROOVE_WAVEFORM_CENTERED
          ASSERT_EQUAL(uploaded_mask[y * 20 + x], uploaded_mask[(height - y - 1) * 20 + x]);
#else
          int bottom = waveform_bottom(x, 20, height, MIN(radius, MIN(20, height) / 2));
          if (y < height - 1 - bottom || y > bottom) ASSERT_EQUAL(uploaded_mask[y * 20 + x], 0);
          if (uploaded_mask[y * 20 + x] && y < bottom) ASSERT_EQUAL(uploaded_mask[(y + 1) * 20 + x], 255);
#endif
        }
      }
      ASSERT_EQUAL(uploaded_mask[(GROOVE_WAVEFORM_CENTERED ? height / 2 : height - 1) * 20 + 10], 255);
      if (!radius && !GROOVE_WAVEFORM_CENTERED) {
        for (int y = 0; y < height; y++) ASSERT_EQUAL(uploaded_mask[y * 20], y == height - 1 ? 255 : 0);
      }
      if (!radius) ASSERT_EQUAL(uploaded_mask[19], 255);
    }
  }
  waveform_cache_free(&app);
  g_ui_runtime.running = false;
  PASS();
}

int main(void) {
  TEST_START("Groove waveform cache");
  test_shared_cache();
  test_failed_refresh();
  test_grid_viewport();
  test_card_margin();
  test_waveform_alignment();
  free(uploaded_mask);
  TEST_END();
}
