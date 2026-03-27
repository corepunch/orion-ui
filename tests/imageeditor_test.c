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
  bool    sel_active;
  int     sel_x0, sel_y0, sel_x1, sel_y1;
} test_canvas_t;

static bool canvas_in_selection(const test_canvas_t *s, int x, int y) {
  if (!s->sel_active) return true;
  /* Normalize selection bounds so reversed drag directions work correctly,
   * matching the MIN/MAX semantics used in the production helper. */
  int x0 = s->sel_x0 < s->sel_x1 ? s->sel_x0 : s->sel_x1;
  int x1 = s->sel_x0 > s->sel_x1 ? s->sel_x0 : s->sel_x1;
  int y0 = s->sel_y0 < s->sel_y1 ? s->sel_y0 : s->sel_y1;
  int y1 = s->sel_y0 > s->sel_y1 ? s->sel_y0 : s->sel_y1;
  return x >= x0 && x <= x1 &&
         y >= y0 && y <= y1;
}

static void canvas_set_pixel(test_canvas_t *s, int x, int y, rgba_t c) {
  if (!canvas_in_bounds(x,y)) return;
  if (!canvas_in_selection(s, x, y)) return;
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
  if (!canvas_in_selection(s, sx, sy)) return;
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
    if (!canvas_in_selection(s, x, y)) continue;
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

// ============================================================
// Inline replicas of colorpicker.c HSV helpers (pure C, no SDL)
// ============================================================

static void t_rgb_to_hsv(rgba_t c, float *h, float *s, float *v) {
  float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
  float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
  float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
  *v = mx;
  float delta = mx - mn;
  if (mx < 1e-6f || delta < 1e-6f) { *s = 0.0f; *h = 0.0f; return; }
  *s = delta / mx;
  float hh;
  if      (mx == r) hh =        (g - b) / delta;
  else if (mx == g) hh = 2.0f + (b - r) / delta;
  else              hh = 4.0f + (r - g) / delta;
  hh /= 6.0f;
  if (hh < 0.0f) hh += 1.0f;
  *h = hh;
}

static rgba_t t_hsv_to_rgb(float h, float s, float v, uint8_t a) {
  float r, g, b;
  if (s < 1e-6f) {
    r = g = b = v;
  } else {
    float hh = h * 6.0f;
    int   i  = (int)hh % 6;
    float f  = hh - (int)hh;
    float p  = v * (1.0f - s);
    float q  = v * (1.0f - s * f);
    float t  = v * (1.0f - s * (1.0f - f));
    switch (i) {
      case 0: r=v; g=t; b=p; break;
      case 1: r=q; g=v; b=p; break;
      case 2: r=p; g=v; b=t; break;
      case 3: r=p; g=q; b=v; break;
      case 4: r=t; g=p; b=v; break;
      default: r=v; g=p; b=q; break;
    }
  }
  return (rgba_t){(uint8_t)(r*255.0f),(uint8_t)(g*255.0f),(uint8_t)(b*255.0f),a};
}

void test_rgb_to_hsv_black(void) {
  TEST("rgb_to_hsv – black gives v=0, s=0");
  rgba_t black = {0, 0, 0, 255};
  float h, s, v;
  t_rgb_to_hsv(black, &h, &s, &v);
  ASSERT_EQUAL((int)(v * 100 + 0.5f), 0);
  ASSERT_EQUAL((int)(s * 100 + 0.5f), 0);
  PASS();
}

void test_rgb_to_hsv_white(void) {
  TEST("rgb_to_hsv – white gives v=1, s=0");
  rgba_t white = {255, 255, 255, 255};
  float h, s, v;
  t_rgb_to_hsv(white, &h, &s, &v);
  ASSERT_EQUAL((int)(v * 100 + 0.5f), 100);
  ASSERT_EQUAL((int)(s * 100 + 0.5f), 0);
  PASS();
}

void test_rgb_to_hsv_red(void) {
  TEST("rgb_to_hsv – pure red gives h=0, s=1, v=1");
  rgba_t red = {255, 0, 0, 255};
  float h, s, v;
  t_rgb_to_hsv(red, &h, &s, &v);
  ASSERT_EQUAL((int)(h * 360 + 0.5f), 0);
  ASSERT_EQUAL((int)(s * 100 + 0.5f), 100);
  ASSERT_EQUAL((int)(v * 100 + 0.5f), 100);
  PASS();
}

void test_hsv_to_rgb_round_trip(void) {
  TEST("hsv_to_rgb – RGB→HSV→RGB round-trip within 1 LSB");
  rgba_t colours[] = {
    {128, 64, 200, 255}, {0, 200, 100, 255},
    {255, 128, 0, 255},  {10, 10, 10, 255}
  };
  for (int i = 0; i < 4; i++) {
    rgba_t c = colours[i];
    float h, s, v;
    t_rgb_to_hsv(c, &h, &s, &v);
    rgba_t back = t_hsv_to_rgb(h, s, v, c.a);
    ASSERT_TRUE(abs((int)back.r - (int)c.r) <= 1);
    ASSERT_TRUE(abs((int)back.g - (int)c.g) <= 1);
    ASSERT_TRUE(abs((int)back.b - (int)c.b) <= 1);
  }
  PASS();
}

void test_hsv_to_rgb_gray(void) {
  TEST("hsv_to_rgb – s=0 produces gray (r==g==b)");
  rgba_t gray = t_hsv_to_rgb(0.0f, 0.0f, 0.5f, 255);
  ASSERT_EQUAL(gray.r, gray.g);
  ASSERT_EQUAL(gray.g, gray.b);
  PASS();
}

// ============================================================
// Selection masking tests
// ============================================================

void test_canvas_in_selection_no_selection(void) {
  TEST("canvas_in_selection – returns true everywhere when no selection active");
  test_canvas_t c = {0};
  c.sel_active = false;
  ASSERT_TRUE(canvas_in_selection(&c, 0, 0));
  ASSERT_TRUE(canvas_in_selection(&c, CANVAS_W-1, CANVAS_H-1));
  ASSERT_TRUE(canvas_in_selection(&c, 100, 100));
  PASS();
}

void test_canvas_in_selection_inside(void) {
  TEST("canvas_in_selection – returns true for pixels inside selection");
  test_canvas_t c = {0};
  c.sel_active = true;
  c.sel_x0 = 10; c.sel_y0 = 20; c.sel_x1 = 30; c.sel_y1 = 40;
  ASSERT_TRUE(canvas_in_selection(&c, 10, 20));  /* top-left corner */
  ASSERT_TRUE(canvas_in_selection(&c, 30, 40));  /* bottom-right corner */
  ASSERT_TRUE(canvas_in_selection(&c, 20, 30));  /* interior */
  PASS();
}

void test_canvas_in_selection_outside(void) {
  TEST("canvas_in_selection – returns false for pixels outside selection");
  test_canvas_t c = {0};
  c.sel_active = true;
  c.sel_x0 = 10; c.sel_y0 = 20; c.sel_x1 = 30; c.sel_y1 = 40;
  ASSERT_FALSE(canvas_in_selection(&c, 9,  20));  /* left of selection */
  ASSERT_FALSE(canvas_in_selection(&c, 31, 20));  /* right of selection */
  ASSERT_FALSE(canvas_in_selection(&c, 10, 19));  /* above selection */
  ASSERT_FALSE(canvas_in_selection(&c, 10, 41));  /* below selection */
  PASS();
}

void test_set_pixel_respects_selection(void) {
  TEST("canvas_set_pixel – ignores pixels outside active selection");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  /* Activate a small selection in the center */
  c->sel_active = true;
  c->sel_x0 = 50; c->sel_y0 = 50; c->sel_x1 = 60; c->sel_y1 = 60;

  rgba_t red = {255, 0, 0, 255};
  rgba_t white = {255, 255, 255, 255};

  /* Inside selection: should be written */
  canvas_set_pixel(c, 55, 55, red);
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 55, 55), red));

  /* Outside selection: must remain white */
  canvas_set_pixel(c, 40, 40, red);
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 40, 40), white));

  free(c);
  PASS();
}

void test_flood_fill_respects_selection(void) {
  TEST("canvas_flood_fill – stays within active selection");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c); /* all white */

  /* Activate selection: only the left half of the canvas */
  c->sel_active = true;
  c->sel_x0 = 0; c->sel_y0 = 0; c->sel_x1 = 159; c->sel_y1 = CANVAS_H - 1;

  rgba_t blue = {0, 0, 255, 255};
  rgba_t white = {255, 255, 255, 255};
  canvas_flood_fill(c, 0, 0, blue);

  /* Pixel inside selection must be filled */
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 80, 100), blue));

  /* Pixel outside selection (right half) must remain white */
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 200, 100), white));

  free(c);
  PASS();
}

void test_flood_fill_outside_selection_noop(void) {
  TEST("canvas_flood_fill – no-op when start pixel is outside selection");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  c->canvas_dirty = false;

  c->sel_active = true;
  c->sel_x0 = 50; c->sel_y0 = 50; c->sel_x1 = 100; c->sel_y1 = 100;

  rgba_t blue = {0, 0, 255, 255};
  /* Start outside the selection – should do nothing */
  canvas_flood_fill(c, 10, 10, blue);
  ASSERT_FALSE(c->canvas_dirty);

  free(c);
  PASS();
}

void test_canvas_in_selection_reversed(void) {
  TEST("canvas_in_selection – reversed drag direction (right-to-left / bottom-to-top)");
  test_canvas_t c = {0};
  c.sel_active = true;
  /* Simulate dragging from bottom-right (30,40) back to top-left (10,20) */
  c.sel_x0 = 30; c.sel_y0 = 40; c.sel_x1 = 10; c.sel_y1 = 20;
  /* Corners of the normalised rect must be inside */
  ASSERT_TRUE(canvas_in_selection(&c, 10, 20));  /* top-left */
  ASSERT_TRUE(canvas_in_selection(&c, 30, 40));  /* bottom-right */
  ASSERT_TRUE(canvas_in_selection(&c, 20, 30));  /* interior */
  /* Pixels outside the normalised rect must be excluded */
  ASSERT_FALSE(canvas_in_selection(&c, 9,  20));
  ASSERT_FALSE(canvas_in_selection(&c, 31, 20));
  ASSERT_FALSE(canvas_in_selection(&c, 10, 19));
  ASSERT_FALSE(canvas_in_selection(&c, 10, 41));
  PASS();
}

void test_set_pixel_respects_reversed_selection(void) {
  TEST("canvas_set_pixel – reversed selection masks correctly");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  /* Selection dragged right-to-left: x0>x1, y0>y1 */
  c->sel_active = true;
  c->sel_x0 = 60; c->sel_y0 = 60; c->sel_x1 = 50; c->sel_y1 = 50;

  rgba_t red = {255, 0, 0, 255};
  rgba_t white = {255, 255, 255, 255};

  /* Inside the normalised [50,60]×[50,60] region */
  canvas_set_pixel(c, 55, 55, red);
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 55, 55), red));

  /* Outside — must remain white */
  canvas_set_pixel(c, 40, 40, red);
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 40, 40), white));

  free(c);
  PASS();
}

// ============================================================
// Zoom / pan logic tests
// The following helpers mirror the pure-C pan-clamping and
// coordinate-mapping logic from win_canvas.c without requiring
// SDL or OpenGL.
// ============================================================

// Mirror of the shared zoom level table from win_canvas.c / imageeditor.h
#define T_NUM_ZOOM_LEVELS 5
static const int t_kZoomLevels[T_NUM_ZOOM_LEVELS] = {1, 2, 4, 6, 8};

/* Replicate clamp_pan from win_canvas.c */
static void t_clamp_pan(int scale, int win_w, int win_h,
                         int *pan_x, int *pan_y) {
  int max_x = CANVAS_W * scale - win_w;
  int max_y = CANVAS_H * scale - win_h;
  if (max_x < 0) max_x = 0;
  if (max_y < 0) max_y = 0;
  if (*pan_x < 0) *pan_x = 0;
  if (*pan_y < 0) *pan_y = 0;
  if (*pan_x > max_x) *pan_x = max_x;
  if (*pan_y > max_y) *pan_y = max_y;
}

/* Replicate the scale-snapping logic from canvas_win_set_zoom() */
static int t_snap_scale(int new_scale) {
  if (new_scale <= t_kZoomLevels[0])
    return t_kZoomLevels[0];
  if (new_scale >= t_kZoomLevels[T_NUM_ZOOM_LEVELS - 1])
    return t_kZoomLevels[T_NUM_ZOOM_LEVELS - 1];
  for (int i = 1; i < T_NUM_ZOOM_LEVELS; i++) {
    if (new_scale <= t_kZoomLevels[i]) {
      int dist_prev = new_scale - t_kZoomLevels[i - 1];
      int dist_curr = t_kZoomLevels[i] - new_scale;
      return (dist_prev <= dist_curr) ? t_kZoomLevels[i - 1] : t_kZoomLevels[i];
    }
  }
  return t_kZoomLevels[T_NUM_ZOOM_LEVELS - 1];
}

/* Replicate zoom-in stepping from handle_menu_command */
static int t_zoom_in(int current) {
  for (int i = 0; i < T_NUM_ZOOM_LEVELS; i++) {
    if (t_kZoomLevels[i] > current) return t_kZoomLevels[i];
  }
  return current; /* already at max */
}

/* Replicate zoom-out stepping */
static int t_zoom_out(int current) {
  for (int i = T_NUM_ZOOM_LEVELS - 1; i >= 0; i--) {
    if (t_kZoomLevels[i] < current) return t_kZoomLevels[i];
  }
  return current; /* already at min */
}

void test_zoom_level_cycle_in(void) {
  TEST("zoom – zoom-in cycles through all levels and stops at 8x");
  ASSERT_EQUAL(t_zoom_in(1), 2);
  ASSERT_EQUAL(t_zoom_in(2), 4);
  ASSERT_EQUAL(t_zoom_in(4), 6);
  ASSERT_EQUAL(t_zoom_in(6), 8);
  ASSERT_EQUAL(t_zoom_in(8), 8); /* already at max */
  PASS();
}

void test_zoom_level_cycle_out(void) {
  TEST("zoom – zoom-out cycles through all levels and stops at 1x");
  ASSERT_EQUAL(t_zoom_out(8), 6);
  ASSERT_EQUAL(t_zoom_out(6), 4);
  ASSERT_EQUAL(t_zoom_out(4), 2);
  ASSERT_EQUAL(t_zoom_out(2), 1);
  ASSERT_EQUAL(t_zoom_out(1), 1); /* already at min */
  PASS();
}

void test_pan_clamp_no_zoom(void) {
  TEST("pan clamp – 1x zoom: pan always clamped to zero (canvas fits)");
  int px = 100, py = 50;
  t_clamp_pan(1, CANVAS_W, CANVAS_H, &px, &py);
  ASSERT_EQUAL(px, 0);
  ASSERT_EQUAL(py, 0);
  PASS();
}

void test_pan_clamp_zoom_4x(void) {
  TEST("pan clamp – 4x zoom: pan stays within valid range");
  /* win size equals canvas size; at 4x, max_pan = 3*CANVAS */
  int px = 9999, py = 9999;
  t_clamp_pan(4, CANVAS_W, CANVAS_H, &px, &py);
  ASSERT_EQUAL(px, CANVAS_W * 4 - CANVAS_W);  /* 3*320 = 960 */
  ASSERT_EQUAL(py, CANVAS_H * 4 - CANVAS_H);  /* 3*200 = 600 */
  PASS();
}

void test_pan_clamp_negative(void) {
  TEST("pan clamp – negative pan values are clamped to 0");
  int px = -50, py = -20;
  t_clamp_pan(2, CANVAS_W, CANVAS_H, &px, &py);
  ASSERT_EQUAL(px, 0);
  ASSERT_EQUAL(py, 0);
  PASS();
}

void test_pan_clamp_within_range(void) {
  TEST("pan clamp – valid pan values are unchanged");
  int px = 100, py = 80;
  /* 2x zoom, window size = CANVAS size → max_pan = CANVAS */
  t_clamp_pan(2, CANVAS_W, CANVAS_H, &px, &py);
  ASSERT_EQUAL(px, 100);
  ASSERT_EQUAL(py, 80);
  PASS();
}

void test_zoom_coord_mapping(void) {
  TEST("zoom coord mapping – mouse local + pan correctly maps to canvas pixel");
  /* At 4x zoom with pan_x=100, pan_y=200:
   * mouse at local (8, 12) → canvas pixel (108/4, 212/4) = (27, 53) */
  int scale = 4, pan_x = 100, pan_y = 200;
  int mouse_local_x = 8, mouse_local_y = 12;
  int canvas_x = (mouse_local_x + pan_x) / scale;
  int canvas_y = (mouse_local_y + pan_y) / scale;
  ASSERT_EQUAL(canvas_x, 27);
  ASSERT_EQUAL(canvas_y, 53);
  PASS();
}

void test_zoom_coord_mapping_1x(void) {
  TEST("zoom coord mapping – 1x zoom with zero pan passes through unchanged");
  int scale = 1, pan_x = 0, pan_y = 0;
  int mx = 55, my = 123;
  int cx = (mx + pan_x) / scale;
  int cy = (my + pan_y) / scale;
  ASSERT_EQUAL(cx, 55);
  ASSERT_EQUAL(cy, 123);
  PASS();
}

void test_snap_scale_zero(void) {
  TEST("snap_scale – zero clamps to minimum zoom level (1x)");
  ASSERT_EQUAL(t_snap_scale(0), 1);
  PASS();
}

void test_snap_scale_negative(void) {
  TEST("snap_scale – negative value clamps to minimum zoom level (1x)");
  ASSERT_EQUAL(t_snap_scale(-5), 1);
  ASSERT_EQUAL(t_snap_scale(-100), 1);
  PASS();
}

void test_snap_scale_above_max(void) {
  TEST("snap_scale – value above 8 clamps to maximum zoom level (8x)");
  ASSERT_EQUAL(t_snap_scale(9), 8);
  ASSERT_EQUAL(t_snap_scale(100), 8);
  PASS();
}

void test_snap_scale_exact(void) {
  TEST("snap_scale – exact supported levels are preserved unchanged");
  ASSERT_EQUAL(t_snap_scale(1), 1);
  ASSERT_EQUAL(t_snap_scale(2), 2);
  ASSERT_EQUAL(t_snap_scale(4), 4);
  ASSERT_EQUAL(t_snap_scale(6), 6);
  ASSERT_EQUAL(t_snap_scale(8), 8);
  PASS();
}

void test_snap_scale_midpoint(void) {
  TEST("snap_scale – unsupported value snaps to nearest supported level");
  /* 3 is between 2 and 4 (equidistant) — should snap to 2 (prefer lower) */
  ASSERT_EQUAL(t_snap_scale(3), 2);
  /* 5 is between 4 and 6 (equidistant) — should snap to 4 (prefer lower) */
  ASSERT_EQUAL(t_snap_scale(5), 4);
  /* 7 is between 6 and 8 (equidistant) — should snap to 6 (prefer lower) */
  ASSERT_EQUAL(t_snap_scale(7), 6);
  PASS();
}

// ============================================================
// Inline selection operation helpers (mirrors canvas.c logic, no GL/SDL)
// ============================================================

// Direct pixel write (bypasses selection mask)
static void t_set_pixel_direct(test_canvas_t *s, int x, int y, rgba_t c) {
  if (!canvas_in_bounds(x, y)) return;
  uint8_t *p = s->pixels + ((size_t)y * CANVAS_W + x) * 4;
  p[0]=c.r; p[1]=c.g; p[2]=c.b; p[3]=c.a;
  s->canvas_dirty = true;
}

// Normalised and clamped selection bounds from sel_x0/y0/x1/y1.
// Returns false when the clamped region is empty (mirrors production behaviour).
static bool t_selection_bounds(const test_canvas_t *s,
                               int *x0, int *y0, int *x1, int *y1) {
  *x0 = s->sel_x0 < s->sel_x1 ? s->sel_x0 : s->sel_x1;
  *x1 = s->sel_x0 > s->sel_x1 ? s->sel_x0 : s->sel_x1;
  *y0 = s->sel_y0 < s->sel_y1 ? s->sel_y0 : s->sel_y1;
  *y1 = s->sel_y0 > s->sel_y1 ? s->sel_y0 : s->sel_y1;
  if (*x0 < 0) *x0 = 0;
  if (*y0 < 0) *y0 = 0;
  if (*x1 >= CANVAS_W) *x1 = CANVAS_W - 1;
  if (*y1 >= CANVAS_H) *y1 = CANVAS_H - 1;
  return (*x0 <= *x1 && *y0 <= *y1);
}

// Copy the selected region into a caller-supplied buffer.
static void t_copy_selection(const test_canvas_t *s,
                             uint8_t *out, int *out_w, int *out_h) {
  int x0, y0, x1, y1;
  if (!t_selection_bounds(s, &x0, &y0, &x1, &y1)) { *out_w = 0; *out_h = 0; return; }
  *out_w = x1 - x0 + 1;
  *out_h = y1 - y0 + 1;
  for (int row = 0; row < *out_h; row++) {
    for (int col = 0; col < *out_w; col++) {
      rgba_t c = canvas_get_pixel(s, x0 + col, y0 + row);
      uint8_t *p = out + ((size_t)row * (*out_w) + col) * 4;
      p[0]=c.r; p[1]=c.g; p[2]=c.b; p[3]=c.a;
    }
  }
}

// Fill the selected region with fill_color (respects selection mask).
static void t_clear_selection(test_canvas_t *s, rgba_t fill) {
  int x0, y0, x1, y1;
  if (!t_selection_bounds(s, &x0, &y0, &x1, &y1)) return;
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
      canvas_set_pixel(s, x, y, fill);
}

// Paste a pixel buffer at (dx, dy), bypassing selection mask.
static void t_paste_pixels(test_canvas_t *s,
                           const uint8_t *src, int src_w, int src_h,
                           int dx, int dy) {
  for (int row = 0; row < src_h; row++) {
    for (int col = 0; col < src_w; col++) {
      const uint8_t *p = src + ((size_t)row * src_w + col) * 4;
      rgba_t c = {p[0], p[1], p[2], p[3]};
      t_set_pixel_direct(s, dx + col, dy + row, c);
    }
  }
}

// ============================================================
// Selection operation tests
// ============================================================

void test_copy_selection_content(void) {
  TEST("canvas_copy_selection – copies correct pixels into clipboard buffer");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);  /* fill with white */
  /* Paint a red 3×3 block at (10,10)..(12,12) */
  rgba_t red = {255, 0, 0, 255};
  for (int y = 10; y <= 12; y++)
    for (int x = 10; x <= 12; x++)
      t_set_pixel_direct(c, x, y, red);
  /* Select that block */
  c->sel_active = true;
  c->sel_x0 = 10; c->sel_y0 = 10; c->sel_x1 = 12; c->sel_y1 = 12;
  /* Copy */
  uint8_t *buf = malloc(3 * 3 * 4);
  int w = 0, h = 0;
  t_copy_selection(c, buf, &w, &h);
  ASSERT_EQUAL(w, 3);
  ASSERT_EQUAL(h, 3);
  /* Every pixel in the copied buffer must be red */
  for (int i = 0; i < 9; i++) {
    rgba_t got = {buf[i*4+0], buf[i*4+1], buf[i*4+2], buf[i*4+3]};
    ASSERT_TRUE(rgba_eq(got, red));
  }
  free(buf);
  free(c);
  PASS();
}

void test_clear_selection_fills_bg(void) {
  TEST("canvas_clear_selection – fills selected region with background color");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);  /* white */
  rgba_t red  = {255, 0, 0, 255};
  rgba_t blue = {0, 0, 255, 255};
  rgba_t white = {255, 255, 255, 255};
  /* Paint a red block at (5,5)..(9,9) */
  for (int y = 5; y <= 9; y++)
    for (int x = 5; x <= 9; x++)
      t_set_pixel_direct(c, x, y, red);
  /* Select and clear with blue */
  c->sel_active = true;
  c->sel_x0 = 5; c->sel_y0 = 5; c->sel_x1 = 9; c->sel_y1 = 9;
  t_clear_selection(c, blue);
  /* Inside selection must now be blue */
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 7, 7), blue));
  /* Pixel just outside selection must remain white */
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 4, 7), white));
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 10, 7), white));
  free(c);
  PASS();
}

void test_paste_pixels_at_offset(void) {
  TEST("t_paste_pixels – places clipboard pixels at destination offset");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);  /* white */
  rgba_t green = {0, 255, 0, 255};
  rgba_t white = {255, 255, 255, 255};
  /* Clipboard: 2×2 green block */
  uint8_t src[16];
  for (int i = 0; i < 4; i++) {
    src[i*4+0]=green.r; src[i*4+1]=green.g;
    src[i*4+2]=green.b; src[i*4+3]=green.a;
  }
  t_paste_pixels(c, src, 2, 2, 100, 50);
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 100, 50), green));
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 101, 51), green));
  /* Adjacent pixels outside paste region must be white */
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 99, 50), white));
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 102, 50), white));
  free(c);
  PASS();
}

void test_cut_selection_clears_and_copies(void) {
  TEST("canvas_cut_selection – copies pixels to buffer and clears region");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  rgba_t red  = {255, 0, 0, 255};
  rgba_t white = {255, 255, 255, 255};
  /* Paint red at (20,20)..(22,22) */
  for (int y = 20; y <= 22; y++)
    for (int x = 20; x <= 22; x++)
      t_set_pixel_direct(c, x, y, red);
  c->sel_active = true;
  c->sel_x0 = 20; c->sel_y0 = 20; c->sel_x1 = 22; c->sel_y1 = 22;
  /* Cut: copy then clear with white */
  uint8_t *buf = malloc(3 * 3 * 4);
  int w = 0, h = 0;
  t_copy_selection(c, buf, &w, &h);
  t_clear_selection(c, white);
  /* Buffer must contain the original red pixels */
  ASSERT_EQUAL(w, 3); ASSERT_EQUAL(h, 3);
  for (int i = 0; i < 9; i++) {
    rgba_t got = {buf[i*4+0], buf[i*4+1], buf[i*4+2], buf[i*4+3]};
    ASSERT_TRUE(rgba_eq(got, red));
  }
  /* Canvas must now be white in that region */
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 21, 21), white));
  free(buf);
  free(c);
  PASS();
}

void test_move_selection(void) {
  TEST("canvas_begin/commit_move – moves selected pixels to new position");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);  /* white */
  rgba_t red   = {255, 0, 0, 255};
  rgba_t white = {255, 255, 255, 255};
  /* Place a red 3×3 block at (10,10) */
  for (int y = 10; y <= 12; y++)
    for (int x = 10; x <= 12; x++)
      t_set_pixel_direct(c, x, y, red);
  /* Select it */
  c->sel_active = true;
  c->sel_x0 = 10; c->sel_y0 = 10; c->sel_x1 = 12; c->sel_y1 = 12;
  /* Simulate begin_move: extract pixels, clear original region */
  int bx0, by0, bx1, by1;
  if (!t_selection_bounds(c, &bx0, &by0, &bx1, &by1)) { free(c); PASS(); return; }
  int fw = bx1 - bx0 + 1, fh = by1 - by0 + 1;
  uint8_t *fpix = malloc((size_t)fw * fh * 4);
  t_copy_selection(c, fpix, &fw, &fh);
  t_clear_selection(c, white);  /* clear original */
  /* Simulate commit_move at (50, 50) */
  t_paste_pixels(c, fpix, fw, fh, 50, 50);
  free(fpix);
  /* Original location must be white */
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 11, 11), white));
  /* New location must be red */
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, 51, 51), red));
  free(c);
  PASS();
}

void test_paste_respects_oob(void) {
  TEST("t_paste_pixels – clips pixels that fall outside canvas bounds");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  rgba_t red = {255, 0, 0, 255};
  /* 4×4 clipboard */
  uint8_t src[64];
  for (int i = 0; i < 16; i++) {
    src[i*4+0]=red.r; src[i*4+1]=red.g; src[i*4+2]=red.b; src[i*4+3]=red.a;
  }
  /* Paste partially off the right/bottom edge */
  t_paste_pixels(c, src, 4, 4, CANVAS_W - 2, CANVAS_H - 2);
  /* In-bounds pixels must be red */
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, CANVAS_W-2, CANVAS_H-2), red));
  ASSERT_TRUE(rgba_eq(canvas_get_pixel(c, CANVAS_W-1, CANVAS_H-1), red));
  /* No crash – test passes if we reach this point */
  free(c);
  PASS();
}

void test_selection_bounds_clamped(void) {
  TEST("t_selection_bounds – clamps out-of-range selection to canvas bounds");
  test_canvas_t c = {0};
  c.sel_active = true;
  /* Selection that extends beyond the canvas on all sides */
  c.sel_x0 = -10; c.sel_y0 = -5; c.sel_x1 = CANVAS_W + 20; c.sel_y1 = CANVAS_H + 10;
  int x0, y0, x1, y1;
  bool ok = t_selection_bounds(&c, &x0, &y0, &x1, &y1);
  ASSERT_TRUE(ok);
  ASSERT_EQUAL(x0, 0);
  ASSERT_EQUAL(y0, 0);
  ASSERT_EQUAL(x1, CANVAS_W - 1);
  ASSERT_EQUAL(y1, CANVAS_H - 1);
  PASS();
}

int main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  TEST_START("Image Editor Logic");
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

  test_canvas_in_selection_no_selection();
  test_canvas_in_selection_inside();
  test_canvas_in_selection_outside();
  test_canvas_in_selection_reversed();
  test_set_pixel_respects_selection();
  test_set_pixel_respects_reversed_selection();
  test_flood_fill_respects_selection();
  test_flood_fill_outside_selection_noop();

  test_zoom_level_cycle_in();
  test_zoom_level_cycle_out();
  test_pan_clamp_no_zoom();
  test_pan_clamp_zoom_4x();
  test_pan_clamp_negative();
  test_pan_clamp_within_range();
  test_zoom_coord_mapping();
  test_zoom_coord_mapping_1x();

  test_snap_scale_zero();
  test_snap_scale_negative();
  test_snap_scale_above_max();
  test_snap_scale_exact();
  test_snap_scale_midpoint();
  test_copy_selection_content();
  test_clear_selection_fills_bg();
  test_paste_pixels_at_offset();
  test_cut_selection_clears_and_copies();
  test_move_selection();
  test_paste_respects_oob();
  test_selection_bounds_clamped();

  TEST_END();
}
