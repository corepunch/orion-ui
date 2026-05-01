// rect_test.c — Unit tests for user/rect.h layout helpers.
//
// All helpers are static inline, so no platform/GL linkage is needed beyond
// what ui.h already pulls in via test_framework.h.  The functions under test
// are pure arithmetic (no window state, no message queue) so this file does
// NOT include test_env.h.

#include "test_framework.h"
#include "../ui.h"
#include "../user/rect.h"

// ── rect_inset ────────────────────────────────────────────────────────────

void test_rect_inset_shrinks(void) {
    TEST("rect_inset: positive d shrinks all four sides");
    irect16_t r = {10, 20, 100, 80};
    irect16_t i = rect_inset(r, 5);
    ASSERT_EQUAL(i.x, 15);
    ASSERT_EQUAL(i.y, 25);
    ASSERT_EQUAL(i.w, 90);
    ASSERT_EQUAL(i.h, 70);
    PASS();
}

void test_rect_inset_expands(void) {
    TEST("rect_inset: negative d expands all four sides");
    irect16_t r = {10, 20, 100, 80};
    irect16_t i = rect_inset(r, -5);
    ASSERT_EQUAL(i.x, 5);
    ASSERT_EQUAL(i.y, 15);
    ASSERT_EQUAL(i.w, 110);
    ASSERT_EQUAL(i.h, 90);
    PASS();
}

void test_rect_inset_zero(void) {
    TEST("rect_inset: d=0 leaves rect unchanged");
    irect16_t r = {10, 20, 100, 80};
    irect16_t i = rect_inset(r, 0);
    ASSERT_EQUAL(i.x, 10);
    ASSERT_EQUAL(i.y, 20);
    ASSERT_EQUAL(i.w, 100);
    ASSERT_EQUAL(i.h, 80);
    PASS();
}

// ── rect_inset_xy ────────────────────────────────────────────────────────

void test_rect_inset_xy(void) {
    TEST("rect_inset_xy: independent horizontal and vertical insets");
    irect16_t r = {0, 0, 100, 60};
    irect16_t i = rect_inset_xy(r, 4, 2);
    ASSERT_EQUAL(i.x, 4);
    ASSERT_EQUAL(i.y, 2);
    ASSERT_EQUAL(i.w, 92);
    ASSERT_EQUAL(i.h, 56);
    PASS();
}

// ── rect_offset ──────────────────────────────────────────────────────────

void test_rect_offset(void) {
    TEST("rect_offset: translates x/y without changing size");
    irect16_t r = {5, 10, 200, 150};
    irect16_t o = rect_offset(r, 3, -4);
    ASSERT_EQUAL(o.x, 8);
    ASSERT_EQUAL(o.y, 6);
    ASSERT_EQUAL(o.w, 200);
    ASSERT_EQUAL(o.h, 150);
    PASS();
}

// ── rect_center ──────────────────────────────────────────────────────────

void test_rect_center(void) {
    TEST("rect_center: centers w×h inside parent rect");
    irect16_t parent = {10, 20, 200, 100};
    irect16_t c = rect_center(parent, 40, 20);
    ASSERT_EQUAL(c.x, 10 + (200 - 40) / 2);
    ASSERT_EQUAL(c.y, 20 + (100 - 20) / 2);
    ASSERT_EQUAL(c.w, 40);
    ASSERT_EQUAL(c.h, 20);
    PASS();
}

void test_rect_center_at_origin(void) {
    TEST("rect_center: centering inside a zero-origin rect");
    irect16_t parent = {0, 0, 100, 60};
    irect16_t c = rect_center(parent, 20, 10);
    ASSERT_EQUAL(c.x, 40);
    ASSERT_EQUAL(c.y, 25);
    ASSERT_EQUAL(c.w, 20);
    ASSERT_EQUAL(c.h, 10);
    PASS();
}

// ── rect_split_left ──────────────────────────────────────────────────────

void test_rect_split_left(void) {
    TEST("rect_split_left: returns left strip of given width");
    irect16_t r = {10, 20, 100, 50};
    irect16_t s = rect_split_left(r, 30);
    ASSERT_EQUAL(s.x, 10);
    ASSERT_EQUAL(s.y, 20);
    ASSERT_EQUAL(s.w, 30);
    ASSERT_EQUAL(s.h, 50);
    PASS();
}

// ── rect_split_top ───────────────────────────────────────────────────────

void test_rect_split_top(void) {
    TEST("rect_split_top: returns top strip of given height");
    irect16_t r = {10, 20, 100, 50};
    irect16_t s = rect_split_top(r, 15);
    ASSERT_EQUAL(s.x, 10);
    ASSERT_EQUAL(s.y, 20);
    ASSERT_EQUAL(s.w, 100);
    ASSERT_EQUAL(s.h, 15);
    PASS();
}

// ── rect_split_right ─────────────────────────────────────────────────────

void test_rect_split_right(void) {
    TEST("rect_split_right: returns right strip of given width");
    irect16_t r = {10, 20, 100, 50};
    irect16_t s = rect_split_right(r, 25);
    ASSERT_EQUAL(s.x, 10 + 100 - 25); // 85
    ASSERT_EQUAL(s.y, 20);
    ASSERT_EQUAL(s.w, 25);
    ASSERT_EQUAL(s.h, 50);
    PASS();
}

// ── rect_split_bottom ────────────────────────────────────────────────────

void test_rect_split_bottom(void) {
    TEST("rect_split_bottom: returns bottom strip of given height");
    irect16_t r = {10, 20, 100, 50};
    irect16_t s = rect_split_bottom(r, 12);
    ASSERT_EQUAL(s.x, 10);
    ASSERT_EQUAL(s.y, 20 + 50 - 12); // 58
    ASSERT_EQUAL(s.w, 100);
    ASSERT_EQUAL(s.h, 12);
    PASS();
}

// ── rect_trim_left ───────────────────────────────────────────────────────

void test_rect_trim_left(void) {
    TEST("rect_trim_left: removes left strip, returns remainder");
    irect16_t r = {10, 20, 100, 50};
    irect16_t t = rect_trim_left(r, 30);
    ASSERT_EQUAL(t.x, 40);
    ASSERT_EQUAL(t.y, 20);
    ASSERT_EQUAL(t.w, 70);
    ASSERT_EQUAL(t.h, 50);
    PASS();
}

// ── rect_trim_top ────────────────────────────────────────────────────────

void test_rect_trim_top(void) {
    TEST("rect_trim_top: removes top strip, returns remainder");
    irect16_t r = {10, 20, 100, 50};
    irect16_t t = rect_trim_top(r, 15);
    ASSERT_EQUAL(t.x, 10);
    ASSERT_EQUAL(t.y, 35);
    ASSERT_EQUAL(t.w, 100);
    ASSERT_EQUAL(t.h, 35);
    PASS();
}

// ── rect_trim_right ──────────────────────────────────────────────────────

void test_rect_trim_right(void) {
    TEST("rect_trim_right: removes right strip, returns remainder");
    irect16_t r = {10, 20, 100, 50};
    irect16_t t = rect_trim_right(r, 25);
    ASSERT_EQUAL(t.x, 10);
    ASSERT_EQUAL(t.y, 20);
    ASSERT_EQUAL(t.w, 75);
    ASSERT_EQUAL(t.h, 50);
    PASS();
}

// ── rect_trim_bottom ─────────────────────────────────────────────────────

void test_rect_trim_bottom(void) {
    TEST("rect_trim_bottom: removes bottom strip, returns remainder");
    irect16_t r = {10, 20, 100, 50};
    irect16_t t = rect_trim_bottom(r, 12);
    ASSERT_EQUAL(t.x, 10);
    ASSERT_EQUAL(t.y, 20);
    ASSERT_EQUAL(t.w, 100);
    ASSERT_EQUAL(t.h, 38);
    PASS();
}

// ── split + trim are complementary ──────────────────────────────────────

void test_split_trim_left_complement(void) {
    TEST("rect_split_left + rect_trim_left cover the full rect");
    irect16_t r = {0, 0, 200, 80};
    int n = 60;
    irect16_t s = rect_split_left(r, n);
    irect16_t t = rect_trim_left(r, n);
    // Together they must span the original width
    ASSERT_EQUAL(s.w + t.w, r.w);
    ASSERT_EQUAL(s.x, r.x);
    ASSERT_EQUAL(t.x, r.x + n);
    PASS();
}

void test_split_trim_top_complement(void) {
    TEST("rect_split_top + rect_trim_top cover the full rect");
    irect16_t r = {5, 5, 200, 80};
    int n = 20;
    irect16_t s = rect_split_top(r, n);
    irect16_t t = rect_trim_top(r, n);
    ASSERT_EQUAL(s.h + t.h, r.h);
    ASSERT_EQUAL(s.y, r.y);
    ASSERT_EQUAL(t.y, r.y + n);
    PASS();
}

void test_split_trim_right_complement(void) {
    TEST("rect_split_right + rect_trim_right cover the full rect");
    irect16_t r = {0, 0, 200, 80};
    int n = 50;
    irect16_t s = rect_split_right(r, n);
    irect16_t t = rect_trim_right(r, n);
    ASSERT_EQUAL(s.w + t.w, r.w);
    ASSERT_EQUAL(t.x, r.x);
    ASSERT_EQUAL(t.w, r.w - n);
    PASS();
}

void test_split_trim_bottom_complement(void) {
    TEST("rect_split_bottom + rect_trim_bottom cover the full rect");
    irect16_t r = {0, 0, 200, 80};
    int n = 30;
    irect16_t s = rect_split_bottom(r, n);
    irect16_t t = rect_trim_bottom(r, n);
    ASSERT_EQUAL(s.h + t.h, r.h);
    ASSERT_EQUAL(t.y, r.y);
    ASSERT_EQUAL(t.h, r.h - n);
    PASS();
}

// ── main ─────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    TEST_START("user/rect.h layout helpers");

    test_rect_inset_shrinks();
    test_rect_inset_expands();
    test_rect_inset_zero();
    test_rect_inset_xy();
    test_rect_offset();
    test_rect_center();
    test_rect_center_at_origin();
    test_rect_split_left();
    test_rect_split_top();
    test_rect_split_right();
    test_rect_split_bottom();
    test_rect_trim_left();
    test_rect_trim_top();
    test_rect_trim_right();
    test_rect_trim_bottom();
    test_split_trim_left_complement();
    test_split_trim_top_complement();
    test_split_trim_right_complement();
    test_split_trim_bottom_complement();

    TEST_END();
}
