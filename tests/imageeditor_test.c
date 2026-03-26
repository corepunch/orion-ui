// Image Editor Unit Tests
// Tests for pure-C logic: canvas operations, color helpers, file-name
// validation, fill algorithm, and tool selection.
// No SDL / OpenGL initialisation is required; these tests run headless.

#include "test_framework.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

// ============================================================
// Inline replicas of the functions under test.
// We duplicate the small, pure-C pieces here so the test file is
// self-contained and does not depend on the SDL/GL link-time symbols
// that imageeditor.c pulls in through ui.h.
// ============================================================

#define CANVAS_W 320
#define CANVAS_H 200

typedef struct { uint8_t r, g, b, a; } rgba_t;

static bool rgba_eq(rgba_t a, rgba_t b) {
  return a.r==b.r && a.g==b.g && a.b==b.b && a.a==b.a;
}

static uint32_t rgba_to_col(rgba_t c) {
  return ((uint32_t)c.a<<24)|((uint32_t)c.b<<16)|((uint32_t)c.g<<8)|(uint32_t)c.r;
}

static bool canvas_in_bounds(int x, int y) {
  return x>=0 && x<CANVAS_W && y>=0 && y<CANVAS_H;
}

typedef struct {
  uint8_t pixels[CANVAS_H * CANVAS_W * 4];
  bool    canvas_dirty;
} test_canvas_t;

static void canvas_set_pixel(test_canvas_t *s, int x, int y, rgba_t c) {
  if (!canvas_in_bounds(x,y)) return;
  uint8_t *p = s->pixels + ((size_t)y*CANVAS_W+x)*4;
  p[0]=c.r; p[1]=c.g; p[2]=c.b; p[3]=c.a;
  s->canvas_dirty = true;
}

static rgba_t canvas_get_pixel(const test_canvas_t *s, int x, int y) {
  if (!canvas_in_bounds(x,y)) return (rgba_t){0,0,0,0};
  const uint8_t *p = s->pixels + ((size_t)y*CANVAS_W+x)*4;
  return (rgba_t){p[0],p[1],p[2],p[3]};
}

static void canvas_clear(test_canvas_t *s) {
  memset(s->pixels, 0xFF, sizeof(s->pixels));
  s->canvas_dirty = true;
}

static void canvas_draw_circle(test_canvas_t *s, int cx, int cy, int r, rgba_t c) {
  for (int dy=-r; dy<=r; dy++)
    for (int dx=-r; dx<=r; dx++)
      if (dx*dx+dy*dy<=r*r) canvas_set_pixel(s,cx+dx,cy+dy,c);
}

static void canvas_draw_line(test_canvas_t *s,
                             int x0,int y0,int x1,int y1,rgba_t c,int r) {
  int dx=abs(x1-x0), sx=x0<x1?1:-1;
  int dy=-abs(y1-y0), sy=y0<y1?1:-1;
  int err=dx+dy;
  for(;;) {
    if (r==0) canvas_set_pixel(s,x0,y0,c);
    else      canvas_draw_circle(s,x0,y0,r,c);
    if (x0==x1&&y0==y1) break;
    int e2=2*err;
    if(e2>=dy){err+=dy;x0+=sx;}
    if(e2<=dx){err+=dx;y0+=sy;}
  }
}

static void canvas_flood_fill(test_canvas_t *s, int sx, int sy, rgba_t fill) {
  rgba_t target = canvas_get_pixel(s,sx,sy);
  if (rgba_eq(target,fill)) return;
  int total = CANVAS_W*CANVAS_H;
  int *stk_x=malloc(sizeof(int)*total), *stk_y=malloc(sizeof(int)*total);
  bool *vis=calloc(total,sizeof(bool));
  if (!stk_x||!stk_y||!vis){free(stk_x);free(stk_y);free(vis);return;}
  int top=0; stk_x[top]=sx; stk_y[top]=sy; top++;
  const int ddx[]={1,-1,0,0}, ddy[]={0,0,1,-1};
  while (top>0) {
    top--; int x=stk_x[top],y=stk_y[top];
    if (!canvas_in_bounds(x,y)) continue;
    int idx=y*CANVAS_W+x;
    if (vis[idx]) continue;
    if (!rgba_eq(canvas_get_pixel(s,x,y),target)) continue;
    vis[idx]=true; canvas_set_pixel(s,x,y,fill);
    for (int d=0;d<4&&top<total;d++){stk_x[top]=x+ddx[d];stk_y[top]=y+ddy[d];top++;}
  }
  free(stk_x); free(stk_y); free(vis);
}

static bool is_png(const char *name) {
  size_t n = strlen(name);
  if (n < 5) return false; // need at least one char before ".png"
  // case-insensitive compare last 4 chars to ".png"
  const char *ext = name + n - 4;
  return (ext[0]=='.' &&
          (ext[1]=='p'||ext[1]=='P') &&
          (ext[2]=='n'||ext[2]=='N') &&
          (ext[3]=='g'||ext[3]=='G'));
}

// ============================================================
// Inline undo/redo implementation for testing (mirrors undo.c logic)
// ============================================================

#ifndef UNDO_MAX
#define UNDO_MAX 20
#endif

typedef struct {
  uint8_t  pixels[CANVAS_H * CANVAS_W * 4];
  bool     canvas_dirty;
  uint8_t *undo_states[UNDO_MAX];
  int      undo_count;
  uint8_t *redo_states[UNDO_MAX];
  int      redo_count;
} test_doc_t;

static size_t kTestSnapSize = CANVAS_H * CANVAS_W * 4;

static void tdoc_clear_stack(uint8_t **states, int *count) {
  for (int i = 0; i < *count; i++) { free(states[i]); states[i] = NULL; }
  *count = 0;
}

static uint8_t *tdoc_snapshot(const test_doc_t *d) {
  uint8_t *s = malloc(kTestSnapSize);
  if (s) memcpy(s, d->pixels, kTestSnapSize);
  return s;
}

static void tdoc_stack_push(uint8_t **states, int *count, uint8_t *snap) {
  if (*count == UNDO_MAX) {
    free(states[0]);
    memmove(states, states + 1, (UNDO_MAX - 1) * sizeof(uint8_t *));
    (*count)--;
  }
  states[(*count)++] = snap;
}

static void tdoc_push_undo(test_doc_t *d) {
  tdoc_clear_stack(d->redo_states, &d->redo_count);
  uint8_t *snap = tdoc_snapshot(d);
  if (snap) tdoc_stack_push(d->undo_states, &d->undo_count, snap);
}

static bool tdoc_undo(test_doc_t *d) {
  if (d->undo_count == 0) return false;
  uint8_t *cur = tdoc_snapshot(d);
  if (cur) tdoc_stack_push(d->redo_states, &d->redo_count, cur);
  d->undo_count--;
  memcpy(d->pixels, d->undo_states[d->undo_count], kTestSnapSize);
  free(d->undo_states[d->undo_count]);
  d->undo_states[d->undo_count] = NULL;
  d->canvas_dirty = true;
  return true;
}

static bool tdoc_redo(test_doc_t *d) {
  if (d->redo_count == 0) return false;
  uint8_t *cur = tdoc_snapshot(d);
  if (cur) tdoc_stack_push(d->undo_states, &d->undo_count, cur);
  d->redo_count--;
  memcpy(d->pixels, d->redo_states[d->redo_count], kTestSnapSize);
  free(d->redo_states[d->redo_count]);
  d->redo_states[d->redo_count] = NULL;
  d->canvas_dirty = true;
  return true;
}

static void tdoc_free(test_doc_t *d) {
  tdoc_clear_stack(d->undo_states, &d->undo_count);
  tdoc_clear_stack(d->redo_states, &d->redo_count);
  free(d);
}

// ============================================================
// Tests
// ============================================================

void test_rgba_eq(void) {
  TEST("rgba_eq – matching colors");
  rgba_t a = {255,0,128,255};
  rgba_t b = {255,0,128,255};
  ASSERT_TRUE(rgba_eq(a,b));
  PASS();
}

void test_rgba_neq(void) {
  TEST("rgba_eq – differing colors");
  rgba_t a = {255,0,0,255};
  rgba_t b = {0,255,0,255};
  ASSERT_FALSE(rgba_eq(a,b));
  PASS();
}

void test_rgba_to_col_white(void) {
  TEST("rgba_to_col – white = 0xFFFFFFFF");
  rgba_t w = {255,255,255,255};
  ASSERT_EQUAL(rgba_to_col(w), 0xFFFFFFFFu);
  PASS();
}

void test_rgba_to_col_black(void) {
  TEST("rgba_to_col – black = 0xFF000000");
  rgba_t b = {0,0,0,255};
  ASSERT_EQUAL(rgba_to_col(b), 0xFF000000u);
  PASS();
}

void test_rgba_to_col_red(void) {
  TEST("rgba_to_col – red: R in LSB, A in MSB");
  rgba_t r = {255,0,0,255};
  // Expected: AA=0xFF, BB=0x00, GG=0x00, RR=0xFF → 0xFF0000FF
  ASSERT_EQUAL(rgba_to_col(r), 0xFF0000FFu);
  PASS();
}

void test_canvas_bounds(void) {
  TEST("canvas_in_bounds – edge and out-of-bounds");
  ASSERT_TRUE(canvas_in_bounds(0, 0));
  ASSERT_TRUE(canvas_in_bounds(CANVAS_W-1, CANVAS_H-1));
  ASSERT_FALSE(canvas_in_bounds(-1, 0));
  ASSERT_FALSE(canvas_in_bounds(0, -1));
  ASSERT_FALSE(canvas_in_bounds(CANVAS_W, 0));
  ASSERT_FALSE(canvas_in_bounds(0, CANVAS_H));
  PASS();
}

void test_canvas_clear(void) {
  TEST("canvas_clear – all pixels become white");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  rgba_t p = canvas_get_pixel(c, 0, 0);
  ASSERT_EQUAL(p.r, 255);
  ASSERT_EQUAL(p.g, 255);
  ASSERT_EQUAL(p.b, 255);
  ASSERT_EQUAL(p.a, 255);
  // Also check a pixel in the interior
  rgba_t q = canvas_get_pixel(c, 160, 100);
  ASSERT_TRUE(rgba_eq(p, q));
  ASSERT_TRUE(c->canvas_dirty);
  free(c);
  PASS();
}

void test_set_get_pixel(void) {
  TEST("canvas_set_pixel / canvas_get_pixel – round-trip");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  rgba_t col = {10, 20, 30, 255};
  canvas_set_pixel(c, 5, 7, col);
  rgba_t got = canvas_get_pixel(c, 5, 7);
  ASSERT_TRUE(rgba_eq(col, got));
  // Neighbouring pixel must be unchanged (white)
  rgba_t nbr = canvas_get_pixel(c, 6, 7);
  ASSERT_EQUAL(nbr.r, 255);
  free(c);
  PASS();
}

void test_set_pixel_out_of_bounds(void) {
  TEST("canvas_set_pixel – out-of-bounds is silently ignored");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  c->canvas_dirty = false;
  canvas_set_pixel(c, -1, 0, (rgba_t){0,0,0,255});
  canvas_set_pixel(c, 0, CANVAS_H, (rgba_t){0,0,0,255});
  // dirty flag must not have changed
  ASSERT_FALSE(c->canvas_dirty);
  free(c);
  PASS();
}

void test_canvas_draw_circle(void) {
  TEST("canvas_draw_circle – centre pixel is coloured");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  rgba_t red = {255,0,0,255};
  canvas_draw_circle(c, 50, 50, 3, red);
  // Centre must be red
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 50, 50), red));
  // Pixel far away must still be white
  rgba_t far_pix = canvas_get_pixel(c, 100, 100);
  ASSERT_EQUAL(far_pix.r, 255);
  ASSERT_EQUAL(far_pix.g, 255);
  free(c);
  PASS();
}

void test_canvas_draw_line_horizontal(void) {
  TEST("canvas_draw_line – horizontal line");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  rgba_t blue = {0,0,255,255};
  canvas_draw_line(c, 10, 10, 20, 10, blue, 0);
  // Every pixel from x=10 to x=20 at y=10 must be blue
  for (int x = 10; x <= 20; x++) {
    ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, x, 10), blue));
  }
  free(c);
  PASS();
}

void test_canvas_draw_line_single_point(void) {
  TEST("canvas_draw_line – start==end draws a single pixel");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  rgba_t grn = {0,255,0,255};
  canvas_draw_line(c, 30, 40, 30, 40, grn, 0);
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 30, 40), grn));
  free(c);
  PASS();
}

void test_flood_fill_simple(void) {
  TEST("canvas_flood_fill – fills contiguous region");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c); // all white

  // Draw a black border box 10×10 at (20,20)
  rgba_t blk = {0,0,0,255};
  for (int x=20;x<30;x++) { canvas_set_pixel(c,x,20,blk); canvas_set_pixel(c,x,29,blk); }
  for (int y=20;y<30;y++) { canvas_set_pixel(c,20,y,blk); canvas_set_pixel(c,29,y,blk); }

  // Fill interior with red
  rgba_t red = {255,0,0,255};
  canvas_flood_fill(c, 25, 25, red);

  // All interior pixels must be red
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 25, 25), red));
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 21, 21), red));

  // Border pixels must remain black
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 20, 20), blk));
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 29, 20), blk));

  // Pixel outside the box must remain white
  rgba_t out = canvas_get_pixel(c, 10, 10);
  ASSERT_EQUAL(out.r, 255);
  free(c);
  PASS();
}

void test_flood_fill_same_color(void) {
  TEST("canvas_flood_fill – no-op when fill == target");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  c->canvas_dirty = false;
  rgba_t white = {255,255,255,255};
  canvas_flood_fill(c, 0, 0, white); // fill white on white
  // dirty flag must not be set
  ASSERT_FALSE(c->canvas_dirty);
  free(c);
  PASS();
}

void test_is_png_valid(void) {
  TEST("is_png – valid .png filenames");
  ASSERT_TRUE(is_png("image.png"));
  ASSERT_TRUE(is_png("image.PNG"));
  ASSERT_TRUE(is_png("image.Png"));
  ASSERT_TRUE(is_png("path/to/file.png"));
  ASSERT_TRUE(is_png("a.png"));
  PASS();
}

void test_is_png_invalid(void) {
  TEST("is_png – non-png filenames return false");
  ASSERT_FALSE(is_png("image.jpg"));
  ASSERT_FALSE(is_png("image.bmp"));
  ASSERT_FALSE(is_png("png"));
  ASSERT_FALSE(is_png(".png"));   // no filename before the extension
  ASSERT_FALSE(is_png(""));
  ASSERT_FALSE(is_png("imagepng"));
  PASS();
}

void test_canvas_pixel_count(void) {
  TEST("canvas buffer size matches CANVAS_W × CANVAS_H × 4");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  ASSERT_EQUAL((int)sizeof(c->pixels), CANVAS_W * CANVAS_H * 4);
  free(c);
  PASS();
}

void test_draw_thick_line(void) {
  TEST("canvas_draw_line with radius – thick horizontal stroke");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  rgba_t col = {50, 100, 150, 255};
  // Draw a thick horizontal line at y=100 with radius 2
  canvas_draw_line(c, 50, 100, 80, 100, col, 2);
  // Pixels within radius of the line must be coloured
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 65, 100), col)); // on line
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 65, 101), col)); // 1 px below
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 65, 102), col)); // 2 px below (r=2)
  // Pixel further away must remain white
  rgba_t far = canvas_get_pixel(c, 65, 105);
  ASSERT_EQUAL(far.r, 255);
  free(c);
  PASS();
}

void test_undo_basic(void) {
  TEST("undo – restores pixel to pre-draw state");
  test_doc_t *d = calloc(1, sizeof(test_doc_t));
  memset(d->pixels, 0xFF, kTestSnapSize); // white canvas

  // Save undo state, then paint a red pixel
  tdoc_push_undo(d);
  rgba_t red = {255, 0, 0, 255};
  uint8_t *p = d->pixels + (10 * CANVAS_W + 20) * 4;
  p[0]=red.r; p[1]=red.g; p[2]=red.b; p[3]=red.a;

  // Pixel should be red now
  uint8_t *q = d->pixels + (10 * CANVAS_W + 20) * 4;
  ASSERT_EQUAL(q[0], 255);
  ASSERT_EQUAL(q[1], 0);

  // Undo should restore white
  ASSERT_TRUE(tdoc_undo(d));
  q = d->pixels + (10 * CANVAS_W + 20) * 4;
  ASSERT_EQUAL(q[0], 255);
  ASSERT_EQUAL(q[1], 255);
  ASSERT_EQUAL(q[2], 255);
  ASSERT_TRUE(d->canvas_dirty);

  tdoc_free(d);
  PASS();
}

void test_undo_empty(void) {
  TEST("undo – returns false when history is empty");
  test_doc_t *d = calloc(1, sizeof(test_doc_t));
  ASSERT_FALSE(tdoc_undo(d));
  tdoc_free(d);
  PASS();
}

void test_redo_after_undo(void) {
  TEST("redo – restores state that was undone");
  test_doc_t *d = calloc(1, sizeof(test_doc_t));
  memset(d->pixels, 0xFF, kTestSnapSize); // white

  // Paint blue, saving undo state first
  tdoc_push_undo(d);
  uint8_t *p = d->pixels + (5 * CANVAS_W + 5) * 4;
  p[0]=0; p[1]=0; p[2]=255; p[3]=255; // blue

  // Undo → back to white
  ASSERT_TRUE(tdoc_undo(d));
  p = d->pixels + (5 * CANVAS_W + 5) * 4;
  ASSERT_EQUAL(p[2], 255); // B channel of white
  ASSERT_EQUAL(p[0], 255); // R channel of white

  // Redo → back to blue
  ASSERT_TRUE(tdoc_redo(d));
  p = d->pixels + (5 * CANVAS_W + 5) * 4;
  ASSERT_EQUAL(p[0], 0);   // R=0 for blue
  ASSERT_EQUAL(p[2], 255); // B=255 for blue

  tdoc_free(d);
  PASS();
}

void test_redo_empty(void) {
  TEST("redo – returns false when redo stack is empty");
  test_doc_t *d = calloc(1, sizeof(test_doc_t));
  ASSERT_FALSE(tdoc_redo(d));
  tdoc_free(d);
  PASS();
}

void test_new_action_clears_redo(void) {
  TEST("push_undo after undo clears redo stack");
  test_doc_t *d = calloc(1, sizeof(test_doc_t));
  memset(d->pixels, 0xFF, kTestSnapSize);

  // State A → paint → State B
  tdoc_push_undo(d);
  d->pixels[0] = 1; // mark as modified

  // Undo B→A; redo stack now has B
  ASSERT_TRUE(tdoc_undo(d));
  ASSERT_EQUAL(d->redo_count, 1);

  // New action from A → State C; redo must be cleared
  tdoc_push_undo(d);
  d->pixels[0] = 2;
  ASSERT_EQUAL(d->redo_count, 0);

  // Redo should now return false
  ASSERT_FALSE(tdoc_redo(d));

  tdoc_free(d);
  PASS();
}

void test_undo_stack_limit(void) {
  TEST("undo stack caps at UNDO_MAX entries");
  test_doc_t *d = calloc(1, sizeof(test_doc_t));
  memset(d->pixels, 0xFF, kTestSnapSize);

  // Push more than UNDO_MAX snapshots
  for (int i = 0; i < UNDO_MAX + 5; i++) {
    tdoc_push_undo(d);
    d->pixels[0] = (uint8_t)i;
  }
  ASSERT_EQUAL(d->undo_count, UNDO_MAX);

  tdoc_free(d);
  PASS();
}

void test_multiple_undo_redo(void) {
  TEST("multiple undo/redo – state traversal");
  test_doc_t *d = calloc(1, sizeof(test_doc_t));
  memset(d->pixels, 0, kTestSnapSize); // black

  // Push 3 states: 0→1→2→3
  d->pixels[0] = 0;
  tdoc_push_undo(d); d->pixels[0] = 1;
  tdoc_push_undo(d); d->pixels[0] = 2;
  tdoc_push_undo(d); d->pixels[0] = 3;

  // Undo: 3→2→1
  ASSERT_TRUE(tdoc_undo(d)); ASSERT_EQUAL(d->pixels[0], 2);
  ASSERT_TRUE(tdoc_undo(d)); ASSERT_EQUAL(d->pixels[0], 1);

  // Redo: 1→2
  ASSERT_TRUE(tdoc_redo(d)); ASSERT_EQUAL(d->pixels[0], 2);

  // Undo again: 2→1
  ASSERT_TRUE(tdoc_undo(d)); ASSERT_EQUAL(d->pixels[0], 1);

  tdoc_free(d);
  PASS();
}

int main(int argc, char *argv[]) {

  test_rgba_eq();
  test_rgba_neq();
  test_rgba_to_col_white();
  test_rgba_to_col_black();
  test_rgba_to_col_red();
  test_canvas_bounds();
  test_canvas_clear();
  test_set_get_pixel();
  test_set_pixel_out_of_bounds();
  test_canvas_draw_circle();
  test_canvas_draw_line_horizontal();
  test_canvas_draw_line_single_point();
  test_flood_fill_simple();
  test_flood_fill_same_color();
  test_is_png_valid();
  test_is_png_invalid();
  test_canvas_pixel_count();
  test_draw_thick_line();

  test_undo_basic();
  test_undo_empty();
  test_redo_after_undo();
  test_redo_empty();
  test_new_action_clears_redo();
  test_undo_stack_limit();
  test_multiple_undo_redo();

  test_rgb_to_hsv_black();
  test_rgb_to_hsv_white();
  test_rgb_to_hsv_red();
  test_hsv_to_rgb_round_trip();
  test_hsv_to_rgb_gray();

  TEST_END();
}
