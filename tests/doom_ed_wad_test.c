#include "test_framework.h"
#include "apps/doom-ed/wad.c"
#include <unistd.h>

static void put_u32(unsigned char *p, uint32_t n) {
  p[0] = n; p[1] = n >> 8; p[2] = n >> 16; p[3] = n >> 24;
}

static bool write_fixture(const char *path, int variant) {
  static const char *const names[] = {"MAP01", "THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS", "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP"};
  unsigned char bytes[12 + 4 + 11 * 16] = {0};
  memcpy(bytes, variant == 1 ? "NOPE" : "PWAD", 4);
  put_u32(bytes + 4, variant == 2 ? UINT32_MAX : 11);
  put_u32(bytes + 8, 16);
  memcpy(bytes + 12, "data", 4);
  for (int i = 0; i < 11; i++) {
    unsigned char *entry = bytes + 16 + i * 16;
    put_u32(entry, 12);
    put_u32(entry + 4, i == 1 ? 4 : 0);
    memcpy(entry + 8, names[i], strlen(names[i]));
  }
  if (variant == 3) put_u32(bytes + 16 + 16, UINT32_MAX);
  if (variant == 4) memcpy(bytes + 16 + 16 + 8, "WRONG\0\0\0", 8);
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  bool ok = fwrite(bytes, 1, variant == 5 ? 15 : sizeof(bytes), f) == (variant == 5 ? 15 : sizeof(bytes));
  return fclose(f) == 0 && ok;
}

static void test_wad(void) {
  TEST("WAD loading, map detection, lump reads, and malformed input");
  const char *temp = getenv("TEMP");
  if (!temp) temp = getenv("TMP");
  if (!temp) temp = getenv("TMPDIR");
  if (!temp) temp = ".";
  char path[4096], data[4];
  int length = snprintf(path, sizeof(path), "%s/orion-doom-wad-XXXXXX", temp);
  ASSERT_TRUE(length > 0 && (size_t)length < sizeof(path));
  int fd = mkstemp(path);
  ASSERT_TRUE(fd >= 0);
  close(fd);
  doom_wad_t wad = {0};
  ASSERT_TRUE(write_fixture(path, 0));
  ASSERT_TRUE(doom_wad_open(&wad, path));
  ASSERT_STR_EQUAL(wad.type, "PWAD");
  ASSERT_EQUAL(wad.count, 11);
  ASSERT_EQUAL(doom_wad_find(&wad, "LINEDEFS"), 2);
  ASSERT_EQUAL(doom_wad_find(&wad, "missing"), -1);
  ASSERT_TRUE(doom_wad_is_map(&wad, 0));
  ASSERT_FALSE(doom_wad_is_map(&wad, 1));
  ASSERT_TRUE(doom_wad_read(&wad, 1, data, sizeof(data)));
  ASSERT_TRUE(!memcmp(data, "data", 4));
  ASSERT_FALSE(doom_wad_read(&wad, 1, data, 3));
  ASSERT_FALSE(doom_wad_read(&wad, 11, data, 4));
  ASSERT_TRUE(doom_wad_read(&wad, 0, NULL, 0));
  ASSERT_FALSE(doom_wad_open(&wad, path));
  doom_wad_close(&wad);
  doom_wad_close(&wad);
  for (int i = 1; i <= 5; i++) {
    ASSERT_TRUE(write_fixture(path, i));
    if (i == 4) {
      ASSERT_TRUE(doom_wad_open(&wad, path));
      ASSERT_FALSE(doom_wad_is_map(&wad, 0));
      doom_wad_close(&wad);
    } else ASSERT_FALSE(doom_wad_open(&wad, path));
  }
  unlink(path);
  PASS();
}

int main(void) {
  TEST_START("Doom editor WAD");
  test_wad();
  TEST_END();
}
