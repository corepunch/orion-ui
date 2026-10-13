#include "test_framework.h"
#include "apps/imageeditor/imageeditor.h"

app_state_t *g_app;

static void test_normal_layer_composite(void) {
  TEST("normal RGBA layers preserve opaque colors and colors over transparency exactly");
  uint8_t lower[] = {255, 255, 255, 255, 91, 123, 207, 0, 33, 77, 131, 255};
  uint8_t upper[] = {0, 0, 0, 128, 73, 109, 197, 128, 29, 83, 173, 255};
  layer_t layers[] = {
    {.pixels = lower, .visible = true, .opacity = 255, .blend_mode = LAYER_BLEND_NORMAL},
    {.pixels = upper, .visible = true, .opacity = 255, .blend_mode = LAYER_BLEND_NORMAL},
  };
  layer_t *stack[] = {&layers[0], &layers[1]};
  canvas_doc_t doc = {.canvas_w = 3, .canvas_h = 1};
  doc.layer.stack = stack; doc.layer.count = 2;
  uint8_t result[12];
  canvas_composite(&doc, result);
  ASSERT_EQUAL(result[0], 187); ASSERT_EQUAL(result[1], 187); ASSERT_EQUAL(result[2], 187);
  ASSERT_EQUAL(result[3], 255);
  ASSERT_EQUAL(memcmp(result + 4, upper + 4, 8), 0);
  layers[1].opacity = 0;
  canvas_composite(&doc, result);
  ASSERT_EQUAL(memcmp(result, lower, 4), 0);
  ASSERT_EQUAL(result[7], 0);
  ASSERT_EQUAL(memcmp(result + 8, lower + 8, 4), 0);
  layers[1].opacity = 128;
  canvas_composite(&doc, result);
  ASSERT_EQUAL(result[4], 73); ASSERT_EQUAL(result[5], 109); ASSERT_EQUAL(result[6], 197);
  ASSERT_EQUAL(result[7], 64);
  PASS();
}

static void test_blend_modes(void) {
  TEST("non-normal blend modes still composite in linear light");
  uint8_t lower[] = {128, 128, 128, 255}, upper[] = {128, 128, 128, 255}, result[4];
  layer_t layers[] = {
    {.pixels = lower, .visible = true, .opacity = 255, .blend_mode = LAYER_BLEND_NORMAL},
    {.pixels = upper, .visible = true, .opacity = 255},
  };
  layer_t *stack[] = {&layers[0], &layers[1]};
  canvas_doc_t doc = {.canvas_w = 1, .canvas_h = 1};
  doc.layer.stack = stack; doc.layer.count = 2;
  const int modes[] = {LAYER_BLEND_MULTIPLY, LAYER_BLEND_SCREEN, LAYER_BLEND_ADD};
  const int expected[] = {61, 167, 176};
  for (int i = 0; i < 3; i++) {
    layers[1].blend_mode = modes[i];
    canvas_composite(&doc, result);
    for (int c = 0; c < 3; c++) ASSERT_EQUAL(result[c], expected[i]);
    ASSERT_EQUAL(result[3], 255);
  }
  PASS();
}

int main(void) {
  TEST_START("Canvas sRGB compositing");
  test_normal_layer_composite(); test_blend_modes();
  TEST_END();
}
