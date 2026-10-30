/*
 * WAD Structure Tests
 *
 * Tests for WAD file parsing logic that can be exercised without a real
 * WAD file on disk:
 *   - is_map_block_valid  (lump-sequence validation)
 *   - find_lump / find_lump_num style name matching
 *   - WAD header and directory structure layout
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

// ── Minimal type stubs ──────────────────────────────────────────────────────

typedef char lumpname_t[8];

typedef struct {
  uint32_t filepos;
  uint32_t size;
  lumpname_t name;
} filelump_t;

typedef struct {
  char identification[4];
  uint32_t numlumps;
  uint32_t infotableofs;
} wadheader_t;

// ── Functions under test (copied from wad.c to isolate from map.h) ──────────

bool is_map_block_valid(filelump_t *dir, int index, int total_lumps) {
  static const char *expected[] = {
    "THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES",
    "SEGS", "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP"
  };
  for (int i = 0; i < 10; i++) {
    if (index + 1 + i >= total_lumps) return false;
    if (strncmp(dir[index + 1 + i].name, expected[i], 8) != 0) return false;
  }
  return true;
}

static int find_lump_num_impl(filelump_t const *dir, int num_lumps, const char *name) {
  for (int i = 0; i < num_lumps; i++) {
    if (strncmp(dir[i].name, name, sizeof(lumpname_t)) == 0)
      return i;
  }
  return -1;
}

// ── Test helpers ─────────────────────────────────────────────────────────────

static int tests_passed = 0;
static int tests_total  = 0;

#define TEST(name) \
  printf("\nTest %d: %s... ", ++tests_total, name); \
  fflush(stdout)

#define PASS() \
  printf("PASSED\n"); \
  tests_passed++

#define ASSERT(cond, msg) \
  if (!(cond)) { \
    printf("FAILED: %s\n", msg); \
    return; \
  }

// Helper: set a lump name, zero-padding to 8 bytes (WAD convention)
static void set_lump_name(filelump_t *lump, const char *name) {
  memset(lump->name, 0, sizeof(lumpname_t));
  strncpy(lump->name, name, sizeof(lumpname_t));
}

// Build a minimal but valid map directory block starting at index 0:
// [0]=marker, [1]=THINGS, ..., [10]=BLOCKMAP
static void build_valid_map_block(filelump_t *dir, const char *marker) {
  static const char *lumps[] = {
    "THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES",
    "SEGS", "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP"
  };
  set_lump_name(&dir[0], marker);
  for (int i = 0; i < 10; i++) {
    set_lump_name(&dir[i + 1], lumps[i]);
    dir[i + 1].filepos = 0;
    dir[i + 1].size    = 0;
  }
}

// ── is_map_block_valid tests ─────────────────────────────────────────────────

static void test_valid_doom_map_e1m1(void) {
  TEST("is_map_block_valid: valid E1M1 marker block");
  filelump_t dir[11];
  memset(dir, 0, sizeof(dir));
  build_valid_map_block(dir, "E1M1");
  ASSERT(is_map_block_valid(dir, 0, 11), "Valid E1M1 map block should pass");
  PASS();
}

static void test_valid_doom2_map01(void) {
  TEST("is_map_block_valid: valid MAP01 marker block");
  filelump_t dir[11];
  memset(dir, 0, sizeof(dir));
  build_valid_map_block(dir, "MAP01");
  ASSERT(is_map_block_valid(dir, 0, 11), "Valid MAP01 map block should pass");
  PASS();
}

static void test_valid_map_block_not_at_start(void) {
  TEST("is_map_block_valid: valid block not at directory start");
  /* Put some other lumps before the map block */
  filelump_t dir[15];
  memset(dir, 0, sizeof(dir));
  set_lump_name(&dir[0], "PLAYPAL");
  set_lump_name(&dir[1], "COLORMAP");
  set_lump_name(&dir[2], "TEXTURE1");
  /* Map block starts at index 3 */
  build_valid_map_block(&dir[3], "E2M1");
  ASSERT(is_map_block_valid(dir, 3, 14), "Valid block at offset 3 should pass");
  PASS();
}

static void test_invalid_wrong_lump_name(void) {
  TEST("is_map_block_valid: wrong lump name fails");
  filelump_t dir[11];
  memset(dir, 0, sizeof(dir));
  build_valid_map_block(dir, "E1M1");
  /* Corrupt the SECTORS lump name */
  set_lump_name(&dir[8], "JUNK");
  ASSERT(!is_map_block_valid(dir, 0, 11),
         "Block with wrong lump name should fail");
  PASS();
}

static void test_invalid_too_few_lumps(void) {
  TEST("is_map_block_valid: too few lumps in directory");
  filelump_t dir[11];
  memset(dir, 0, sizeof(dir));
  build_valid_map_block(dir, "E1M1");
  /* total_lumps = 5 means we can't reach all 10 required lumps */
  ASSERT(!is_map_block_valid(dir, 0, 5),
         "Block with insufficient total_lumps should fail");
  PASS();
}

static void test_invalid_index_near_end(void) {
  TEST("is_map_block_valid: index too close to end of directory");
  filelump_t dir[11];
  memset(dir, 0, sizeof(dir));
  build_valid_map_block(dir, "E1M1");
  /* index=8 means there are only 2 lumps after it, not 10 */
  ASSERT(!is_map_block_valid(dir, 8, 11),
         "Block index leaving too few trailing lumps should fail");
  PASS();
}

static void test_valid_all_doom2_maps(void) {
  TEST("is_map_block_valid: all 32 MAP0x/MAP1x/MAP2x/MAP3x markers validate");
  const char *markers[] = {
    "MAP01","MAP02","MAP03","MAP04","MAP05","MAP06","MAP07","MAP08","MAP09","MAP10",
    "MAP11","MAP12","MAP13","MAP14","MAP15","MAP16","MAP17","MAP18","MAP19","MAP20",
    "MAP21","MAP22","MAP23","MAP24","MAP25","MAP26","MAP27","MAP28","MAP29","MAP30",
    "MAP31","MAP32"
  };
  for (int m = 0; m < 32; m++) {
    filelump_t dir[11];
    memset(dir, 0, sizeof(dir));
    build_valid_map_block(dir, markers[m]);
    ASSERT(is_map_block_valid(dir, 0, 11), markers[m]);
  }
  PASS();
}

// ── Lump name matching tests ─────────────────────────────────────────────────

static void test_find_lump_found(void) {
  TEST("find_lump_num: finds existing lump by name");
  filelump_t dir[4];
  memset(dir, 0, sizeof(dir));
  set_lump_name(&dir[0], "PLAYPAL");
  set_lump_name(&dir[1], "COLORMAP");
  set_lump_name(&dir[2], "TEXTURE1");
  set_lump_name(&dir[3], "PNAMES");

  ASSERT(find_lump_num_impl(dir, 4, "TEXTURE1") == 2, "TEXTURE1 should be at index 2");
  PASS();
}

static void test_find_lump_not_found(void) {
  TEST("find_lump_num: returns -1 for missing lump");
  filelump_t dir[2];
  memset(dir, 0, sizeof(dir));
  set_lump_name(&dir[0], "PLAYPAL");
  set_lump_name(&dir[1], "COLORMAP");

  ASSERT(find_lump_num_impl(dir, 2, "MISSING") == -1,
         "Missing lump should return -1");
  PASS();
}

static void test_find_lump_first_match(void) {
  TEST("find_lump_num: returns first matching lump when duplicates exist");
  filelump_t dir[4];
  memset(dir, 0, sizeof(dir));
  set_lump_name(&dir[0], "FLAT1");
  set_lump_name(&dir[1], "FLAT2");
  set_lump_name(&dir[2], "FLAT1");  // duplicate
  set_lump_name(&dir[3], "FLAT3");

  ASSERT(find_lump_num_impl(dir, 4, "FLAT1") == 0,
         "Should return first occurrence of duplicate lump");
  PASS();
}

static void test_find_lump_empty_directory(void) {
  TEST("find_lump_num: empty directory returns -1");
  ASSERT(find_lump_num_impl(NULL, 0, "ANYTHING") == -1,
         "Empty directory should return -1");
  PASS();
}

static void test_find_lump_8char_name(void) {
  TEST("find_lump_num: 8-character lump name (no null terminator) matches");
  filelump_t dir[1];
  memset(dir, 0, sizeof(dir));
  /* Write all 8 bytes without a null terminator */
  memcpy(dir[0].name, "LONGNAME", 8);

  ASSERT(find_lump_num_impl(dir, 1, "LONGNAME") == 0,
         "8-character lump name should match");
  PASS();
}

// ── WAD header structure tests ───────────────────────────────────────────────

static void test_wadheader_size(void) {
  TEST("wadheader_t: struct size is 12 bytes (WAD spec)");
  ASSERT(sizeof(wadheader_t) == 12,
         "WAD header must be exactly 12 bytes per specification");
  PASS();
}

static void test_filelump_size(void) {
  TEST("filelump_t: struct size is 16 bytes (WAD spec)");
  /* 4 (filepos) + 4 (size) + 8 (name) = 16 */
  ASSERT(sizeof(filelump_t) == 16,
         "WAD directory entry must be exactly 16 bytes per specification");
  PASS();
}

static void test_wadheader_identification_iwad(void) {
  TEST("wadheader_t: IWAD identification field parses correctly");
  wadheader_t hdr;
  memset(&hdr, 0, sizeof(hdr));
  memcpy(hdr.identification, "IWAD", 4);
  ASSERT(strncmp(hdr.identification, "IWAD", 4) == 0,
         "IWAD identification should parse correctly");
  PASS();
}

static void test_wadheader_identification_pwad(void) {
  TEST("wadheader_t: PWAD identification field parses correctly");
  wadheader_t hdr;
  memset(&hdr, 0, sizeof(hdr));
  memcpy(hdr.identification, "PWAD", 4);
  ASSERT(strncmp(hdr.identification, "PWAD", 4) == 0,
         "PWAD identification should parse correctly");
  PASS();
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(void) {
  printf("\n=== Running WAD Structure Tests ===\n");

  /* is_map_block_valid */
  test_valid_doom_map_e1m1();
  test_valid_doom2_map01();
  test_valid_map_block_not_at_start();
  test_invalid_wrong_lump_name();
  test_invalid_too_few_lumps();
  test_invalid_index_near_end();
  test_valid_all_doom2_maps();

  /* find_lump_num */
  test_find_lump_found();
  test_find_lump_not_found();
  test_find_lump_first_match();
  test_find_lump_empty_directory();
  test_find_lump_8char_name();

  /* WAD struct layout */
  test_wadheader_size();
  test_filelump_size();
  test_wadheader_identification_iwad();
  test_wadheader_identification_pwad();

  printf("\n=== Test Results ===\n");
  printf("Passed: %d/%d\n", tests_passed, tests_total);

  if (tests_passed == tests_total) {
    printf("\n=== All Tests Passed! ===\n");
    return 0;
  }
  printf("\n=== Some Tests Failed ===\n");
  return 1;
}
