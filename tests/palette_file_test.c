#include "test_framework.h"
#include <orion/kernel/kernel.h>
#include "apps/imageeditor/palette.h"
#include <unistd.h>

static const uint8_t fixture[] = {
  'R','I','F','F', 28,0,0,0, 'P','A','L',' ', 'd','a','t','a', 16,0,0,0,
  0,3,3,0, 255,0,0,0, 0,0,0,0, 0,0,255,0,
};

static void test_default_palette(void) {
  TEST("shared binary palette preserves the original colors and provides 16 dark/light pairs");
  uint8_t colors[256][4]; int count;
  ASSERT_TRUE(pencil_palette_load_default(ui_get_exe_dir(), colors, &count));
  ASSERT_EQUAL(count, 32);
  ASSERT_EQUAL(colors[0][0], 0x4a); ASSERT_EQUAL(colors[0][1], 0x28); ASSERT_EQUAL(colors[0][2], 0x78);
  ASSERT_EQUAL(colors[1][0], 0xfb); ASSERT_EQUAL(colors[1][1], 0xfc); ASSERT_EQUAL(colors[1][2], 0xff);
  for (int i = 0; i < 32; i += 2) {
    int dark = 77 * colors[i][0] + 150 * colors[i][1] + 29 * colors[i][2];
    int light = 77 * colors[i + 1][0] + 150 * colors[i + 1][1] + 29 * colors[i + 1][2];
    ASSERT_TRUE(dark < light); ASSERT_EQUAL(colors[i][3], 255); ASSERT_EQUAL(colors[i + 1][3], 255);
  }
  ASSERT_EQUAL(colors[255][3], 0);
  PASS();
}

static void test_riff_validation(void) {
  TEST("RIFF PAL decodes RGB/flags and rejects malformed files without modifying output");
  char path[] = "/tmp/orion-palette-XXXXXX";
  int fd = mkstemp(path); ASSERT_TRUE(fd >= 0); close(fd);
  uint8_t input[sizeof(fixture)], colors[256][4]; int count = 0;
  memcpy(input, fixture, sizeof(input));
  input[27] = 4; // Windows palette flags are not alpha.
  FILE *fp = fopen(path, "wb"); ASSERT_NOT_NULL(fp);
  ASSERT_EQUAL(fwrite(input, 1, sizeof(input), fp), sizeof(input)); fclose(fp);
  ASSERT_TRUE(riff_palette_load(path, colors, &count)); ASSERT_EQUAL(count, 3);
  ASSERT_EQUAL(colors[0][0], 255); ASSERT_EQUAL(colors[0][3], 255);
  ASSERT_EQUAL(colors[1][0], 0); ASSERT_EQUAL(colors[2][2], 255);
  const int offsets[] = {0, 4, 8, 16, 21, 22};
  for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
    memcpy(input, fixture, sizeof(input)); input[offsets[i]] ^= 0xff;
    fp = fopen(path, "wb"); ASSERT_NOT_NULL(fp);
    ASSERT_EQUAL(fwrite(input, 1, sizeof(input), fp), sizeof(input)); fclose(fp);
    memset(colors, 0xab, sizeof(colors)); count = 123;
    ASSERT_FALSE(riff_palette_load(path, colors, &count));
    ASSERT_EQUAL(count, 123); ASSERT_EQUAL(colors[0][0], 0xab);
  }
  fp = fopen(path, "wb"); ASSERT_NOT_NULL(fp);
  ASSERT_EQUAL(fwrite(fixture, 1, sizeof(fixture) - 1, fp), sizeof(fixture) - 1); fclose(fp);
  ASSERT_FALSE(riff_palette_load(path, colors, &count));
  unlink(path); PASS();
}

int main(void) {
  TEST_START("RIFF palette files");
  test_default_palette(); test_riff_validation();
  TEST_END();
}
