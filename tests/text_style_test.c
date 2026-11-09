// Text properties from CSS: text-transform, letter-spacing, text-align and
// line-height change how text is measured and set. The test compiles its own
// text engine over a font cache with stubbed textures, and records the glyph
// quads draw calls would send to the GPU.

#include "test_framework.h"
#include "test_env.h"
#include <orion/kernel/renderer.h>
#include <orion/user/ui_style.h>

static uint32_t stub_texture(int w, int h, const void *p, R_TextureFilter f, R_TextureWrap wr) { return 1; }
static bool stub_update(uint32_t t, int x, int y, int w, int h, const void *p) { return true; }
static void stub_delete(uint32_t t) {}
static int stub_max_texture(void) { return 16384; }
#define R_GetMaxTextureSize stub_max_texture
#define R_CreateTextureR8 stub_texture
#define R_UpdateTextureR8 stub_update
#define R_DeleteTexture   stub_delete
#include <orion/user/font_cache.c>
#undef R_GetMaxTextureSize
#undef R_CreateTextureR8
#undef R_UpdateTextureR8
#undef R_DeleteTexture

// Quads drawn: their left edges, in order.
static float g_quad_x[256];
static float g_quad_y[256];
static int g_quads;
static void stub_mesh_init(R_Mesh *m, const R_VertexAttrib *a, int n, size_t stride, GLenum mode) {}
static void stub_mesh_destroy(R_Mesh *m) {}
static void stub_blend(bool on) {}
static void stub_bind(const R_Texture *t) {}
static void stub_draw(R_Mesh *m, const void *vertices, size_t count) {
  const float *v = (const float *)vertices;   // x, y, u, v, colour: 5 words per vertex
  for (size_t i = 0; i < count && g_quads < 256; i += 6, g_quads++) {
    g_quad_x[g_quads] = v[i * 5];
    g_quad_y[g_quads] = v[i * 5 + 1];
  }
}
#define R_MeshInit         stub_mesh_init
#define R_MeshDestroy      stub_mesh_destroy
#define R_SetBlendMode     stub_blend
#define R_TextureBind      stub_bind
#define R_MeshDrawDynamic  stub_draw
#define push_sprite_args   stub_sprite_args
static void stub_sprite_args(int tex, int x, int y, int w, int h, float alpha) {}
#include <orion/user/text.c>
#undef push_sprite_args

static void with_sheet(const char *css) {
  ui_set_app_stylesheet(css, "text-style.css");
  init_text_rendering();
}

void test_uppercase_measures_capitals(void) {
  TEST("text-transform: uppercase measures and draws capitals");
  with_sheet("Upper { text-transform: uppercase; }");
  ASSERT_EQUAL(text_strwidth("Upper", "worktree"), text_strwidth("Text", "WORKTREE"));
  ASSERT_TRUE(text_strwidth("Upper", "worktree") > text_strwidth("Text", "worktree"));
  ASSERT_EQUAL(text_strwidth("Upper", "\xD0\xB2\xD0\xB5\xD1\x82\xD0\xBA\xD0\xB0"),   // ветка
               text_strwidth("Text", "\xD0\x92\xD0\x95\xD0\xA2\xD0\x9A\xD0\x90"));    // ВЕТКА
  PASS();
}

void test_case_mapping(void) {
  TEST("case mapping covers Latin-1, Latin Extended-A, Greek and Cyrillic");
  ASSERT_EQUAL(text_upper(0xE9), 0xC9);     // é
  ASSERT_EQUAL(text_upper(0x107), 0x106);   // ć
  ASSERT_EQUAL(text_upper(0x13E), 0x13D);   // ľ (odd capitals)
  ASSERT_EQUAL(text_upper(0x3C2), 0x3A3);   // final sigma
  ASSERT_EQUAL(text_upper(0x451), 0x401);   // ё
  ASSERT_EQUAL(text_lower(0x401), 0x451);
  ASSERT_EQUAL(text_lower(0x13D), 0x13E);
  ASSERT_EQUAL(text_lower('Q'), 'q');
  ASSERT_EQUAL(text_upper('7'), '7');
  PASS();
}

void test_capitalize(void) {
  TEST("text-transform: capitalize raises the first letter of each word");
  with_sheet("Cap { text-transform: capitalize; }");
  ASSERT_EQUAL(text_strwidth("Cap", "launch targets"), text_strwidth("Text", "Launch Targets"));
  PASS();
}

void test_letter_spacing(void) {
  TEST("letter-spacing adds after every character, in px or em");
  with_sheet("Px { letter-spacing: 2px; } Em { font: 400 20px \"IBM Plex Sans\"; letter-spacing: .1em; }"
             "Plain20 { font: 400 20px \"IBM Plex Sans\"; }");
  float scale = text_platform_scale();
  int extra = text_strwidth("Px", "a b c") - text_strwidth("Text", "a b c");
  ASSERT_TRUE(abs(extra - (int)lroundf(5 * 2 * scale)) <= 1);
  int em = text_strwidth("Em", "abcd") - text_strwidth("Plain20", "abcd");
  ASSERT_TRUE(abs(em - (int)lroundf(4 * 2.0f * scale)) <= 1);
  PASS();
}

void test_align_single_line(void) {
  TEST("text-align places one line in its viewport unless the caller aligns it");
  with_sheet("Right { text-align: right; } Center { text-align: center; }");
  g_ui_runtime.running = true;
  irect16_t vp = {10, 0, 200, 20};
  int w = text_strwidth("Right", "Hi");
  g_quads = 0; draw_text_clipped("Right", "Hi", &vp, 0xFFFFFFFF, 0);
  float right_x = g_quad_x[0];
  g_quads = 0; draw_text_clipped("Text", "Hi", &(irect16_t){10 + 200 - w, 0, w, 20}, 0xFFFFFFFF, 0);
  ASSERT_TRUE(fabsf(right_x - g_quad_x[0]) < 0.01f);
  g_quads = 0; draw_text_clipped("Center", "Hi", &vp, 0xFFFFFFFF, 0);
  float center_x = g_quad_x[0];
  g_quads = 0; draw_text_clipped("Text", "Hi", &(irect16_t){10 + (200 - w) / 2, 0, w, 20}, 0xFFFFFFFF, 0);
  ASSERT_TRUE(fabsf(center_x - g_quad_x[0]) < 0.01f);
  g_quads = 0; draw_text_clipped("Right", "Hi", &vp, 0xFFFFFFFF, TEXT_ALIGN_CENTER);   // the caller wins
  ASSERT_TRUE(fabsf(center_x - g_quad_x[0]) < 0.01f);
  g_ui_runtime.running = false;
  PASS();
}

void test_align_wrapped_lines(void) {
  TEST("text-align: right sets each wrapped line flush right, trailing spaces hanging");
  with_sheet("Right { text-align: right; }");
  g_ui_runtime.running = true;
  int word = text_strwidth("Right", "ab");
  irect16_t vp = {0, 0, word * 2, 100};   // room for one word per line, not two with a space
  g_quads = 0;
  text_wrap_result_t r = text_wrap_layout_font("Right", "ab ab", &vp, 0xFFFFFFFF, true);
  ASSERT_TRUE(r.wrapped);
  ASSERT_EQUAL(g_quads, 4);
  ASSERT_TRUE(fabsf(g_quad_x[0] - g_quad_x[2]) < 0.01f);   // both lines start at the same x
  ASSERT_TRUE(g_quad_x[0] > word / 2);                    // moved right, past the trailing space
  g_ui_runtime.running = false;
  PASS();
}

void test_line_height(void) {
  TEST("line-height spaces wrapped lines and centres glyphs in the line box");
  with_sheet("Tall { line-height: 40px; }");
  int word = text_strwidth("Tall", "ab") + 1;   // one word per line
  float scale = text_platform_scale();
  ASSERT_EQUAL(calc_text_height_font("Tall", "ab ab ab", word), 3 * (int)lroundf(40 * scale));
  ASSERT_EQUAL(calc_text_height_font("Text", "ab ab ab", word), 3 * text_char_height("Text"));
  PASS();
}

int main(void) {
  TEST_START("Text properties");
  test_env_init();
  test_uppercase_measures_capitals();
  test_case_mapping();
  test_capitalize();
  test_letter_spacing();
  test_align_single_line();
  test_align_wrapped_lines();
  test_line_height();
  ui_set_app_stylesheet(NULL, NULL);
  test_env_shutdown();
  TEST_END();
}
