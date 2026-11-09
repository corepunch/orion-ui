// Text cell grid tests: ANSI lines become code points and colours.

#include "test_framework.h"
#include <orion/user/text.h>
#include <orion/user/ansi.h>

static const uint32_t FG = 0xFFD4D4D4u, BG = 0xFF1E1E1Eu;

static uint32_t abgr(uint32_t argb) {
  return (argb & 0xFF00FF00u) | ((argb >> 16) & 0xFFu) | ((argb & 0xFFu) << 16);
}

void test_null_and_empty(void) {
  TEST("text_cells_from_ansi handles NULL, empty and zero capacity");
  text_cell_t cells[4];
  ASSERT_EQUAL(text_cells_from_ansi(NULL, cells, 4, FG, BG), 0);
  ASSERT_EQUAL(text_cells_from_ansi("", cells, 4, FG, BG), 0);
  ASSERT_EQUAL(text_cells_from_ansi("abc", cells, 0, FG, BG), 0);
  PASS();
}

void test_plain_text_keeps_colours(void) {
  TEST("plain text keeps the caller's exact colours");
  text_cell_t cells[8];
  ASSERT_EQUAL(text_cells_from_ansi("ab", cells, 8, FG, BG), 2);
  ASSERT_EQUAL(cells[0].codepoint, 'a');
  ASSERT_EQUAL(cells[1].codepoint, 'b');
  ASSERT_EQUAL(cells[0].fg, FG);
  ASSERT_EQUAL(cells[0].bg, BG);
  PASS();
}

void test_utf8_is_one_cell(void) {
  TEST("a UTF-8 sequence is one cell holding its code point");
  text_cell_t cells[8];
  ASSERT_EQUAL(text_cells_from_ansi("\xD0\xAF\xE2\x94\x80", cells, 8, FG, BG), 2);   // Я ─
  ASSERT_EQUAL(cells[0].codepoint, 0x42F);
  ASSERT_EQUAL(cells[1].codepoint, 0x2500);
  PASS();
}

void test_sgr_colours_and_reset(void) {
  TEST("SGR 31 colours the following cells; SGR 0 restores the defaults");
  text_cell_t cells[8];
  ASSERT_EQUAL(text_cells_from_ansi("\x1b[31mr\x1b[0mn", cells, 8, FG, BG), 2);
  ASSERT_EQUAL(cells[0].codepoint, 'r');
  ASSERT_EQUAL(cells[0].fg, abgr(kAnsi16[1]));
  ASSERT_EQUAL(cells[1].fg, FG);
  PASS();
}

void test_capacity(void) {
  TEST("text_cells_from_ansi stops at the capacity");
  text_cell_t cells[3];
  ASSERT_EQUAL(text_cells_from_ansi("abcdef", cells, 3, FG, BG), 3);
  ASSERT_EQUAL(cells[2].codepoint, 'c');
  PASS();
}

int main(void) {
  TEST_START("Text cell grid");
  test_null_and_empty();
  test_plain_text_keeps_colours();
  test_utf8_is_one_cell();
  test_sgr_colours_and_reset();
  test_capacity();
  TEST_END();
}
