// Image Editor Unit Tests
// Tests for pure-C logic: canvas operations, color helpers, file-name
// validation, fill algorithm, and tool selection.
// No SDL / OpenGL initialisation is required; these tests run headless.

#include "test_framework.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>

// ============================================================
// Inline replicas of the functions under test.
// We duplicate the small, pure-C pieces here so the test file is
// self-contained and does not depend on the SDL/GL link-time symbols
// that imageeditor.c pulls in through ui.h.
// ============================================================

#define CANVAS_W 320
#define CANVAS_H 200

#define MAKE_COLOR(r,g,b,a) \
  (((uint32_t)(uint8_t)(a)<<24)|((uint32_t)(uint8_t)(b)<<16)|((uint32_t)(uint8_t)(g)<<8)|(uint32_t)(uint8_t)(r))
#define COLOR_R(c) ((uint8_t)((c) & 0xFF))
#define COLOR_G(c) ((uint8_t)(((c) >> 8) & 0xFF))
#define COLOR_B(c) ((uint8_t)(((c) >> 16) & 0xFF))
#define COLOR_A(c) ((uint8_t)(((c) >> 24) & 0xFF))

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

static void canvas_set_pixel(test_canvas_t *s, int x, int y, uint32_t c) {
  if (!canvas_in_bounds(x,y)) return;
  if (!canvas_in_selection(s, x, y)) return;
  uint8_t *p = s->pixels + ((size_t)y*CANVAS_W+x)*4;
  p[0]=COLOR_R(c); p[1]=COLOR_G(c); p[2]=COLOR_B(c); p[3]=COLOR_A(c);
  s->canvas_dirty = true;
}

static uint32_t canvas_get_pixel(const test_canvas_t *s, int x, int y) {
  if (!canvas_in_bounds(x,y)) return MAKE_COLOR(0,0,0,0);
  const uint8_t *p = s->pixels + ((size_t)y*CANVAS_W+x)*4;
  return MAKE_COLOR(p[0],p[1],p[2],p[3]);
}

static void canvas_clear(test_canvas_t *s) {
  memset(s->pixels, 0xFF, sizeof(s->pixels));
  s->canvas_dirty = true;
}

static void canvas_draw_circle(test_canvas_t *s, int cx, int cy, int r, uint32_t c) {
  for (int dy=-r; dy<=r; dy++)
    for (int dx=-r; dx<=r; dx++)
      if (dx*dx+dy*dy<=r*r) canvas_set_pixel(s,cx+dx,cy+dy,c);
}

static void canvas_draw_line(test_canvas_t *s,
                             int x0,int y0,int x1,int y1,uint32_t c,int r) {
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

static void canvas_flood_fill(test_canvas_t *s, int sx, int sy, uint32_t fill) {
  if (!canvas_in_selection(s, sx, sy)) return;
  uint32_t target = canvas_get_pixel(s,sx,sy);
  if (target == fill) return;
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
    if (canvas_get_pixel(s,x,y) != target) continue;
    vis[idx]=true; canvas_set_pixel(s,x,y,fill);
    for (int d=0;d<4&&top<total;d++){stk_x[top]=x+ddx[d];stk_y[top]=y+ddy[d];top++;}
  }
  free(stk_x); free(stk_y); free(vis);
}

static bool is_png(const char *path) {
  if (!path || !path[0]) return false;
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  unsigned char hdr[4];
  bool ok = (fread(hdr, 1, 4, f) == 4) &&
            hdr[0] == 0x89 && hdr[1] == 0x50 &&
            hdr[2] == 0x4E && hdr[3] == 0x47;
  fclose(f);
  return ok;
}

static const char *is_png_temp_dir(void) {
  const char *d = getenv("TEMP");
  if (!d) d = getenv("TMP");
  if (!d) d = getenv("TMPDIR");
  if (!d) d = "/tmp";
  return d;
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
// Inline helpers for new canvas operations (flip / invert / resize)
// Mirrors the production implementations in canvas.c without SDL/GL deps.
// ============================================================

static void t_flip_h(test_canvas_t *c) {
  int w = CANVAS_W, h = CANVAS_H;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w / 2; x++) {
      uint8_t *l = c->pixels + ((size_t)y * w + x) * 4;
      uint8_t *r = c->pixels + ((size_t)y * w + (w - 1 - x)) * 4;
      uint8_t tmp[4];
      memcpy(tmp, l, 4); memcpy(l, r, 4); memcpy(r, tmp, 4);
    }
  }
  c->canvas_dirty = true;
}

static void t_flip_v(test_canvas_t *c) {
  int w = CANVAS_W, h = CANVAS_H;
  size_t row = (size_t)w * 4;
  uint8_t *tmp = malloc(row);
  if (!tmp) return;
  for (int y = 0; y < h / 2; y++) {
    uint8_t *top = c->pixels + (size_t)y * row;
    uint8_t *bot = c->pixels + (size_t)(h - 1 - y) * row;
    memcpy(tmp, top, row); memcpy(top, bot, row); memcpy(bot, tmp, row);
  }
  free(tmp);
  c->canvas_dirty = true;
}

static void t_invert_colors(test_canvas_t *c) {
  size_t n = (size_t)CANVAS_W * CANVAS_H;
  for (size_t i = 0; i < n; i++) {
    uint8_t *p = c->pixels + i * 4;
    p[0] = (uint8_t)(255 - p[0]);
    p[1] = (uint8_t)(255 - p[1]);
    p[2] = (uint8_t)(255 - p[2]);
    // alpha unchanged
  }
  c->canvas_dirty = true;
}

// Dynamic canvas for resize tests (mirrors canvas_doc_t without GL/SDL deps).
typedef struct {
  uint8_t *pixels;
  int      w, h;
  bool     dirty;
} dyn_canvas_t;

static dyn_canvas_t *dyn_canvas_create(int w, int h) {
  dyn_canvas_t *c = calloc(1, sizeof(dyn_canvas_t));
  if (!c) return NULL;
  c->pixels = malloc((size_t)w * h * 4);
  if (!c->pixels) { free(c); return NULL; }
  memset(c->pixels, 0xFF, (size_t)w * h * 4);  /* white */
  c->w = w; c->h = h;
  return c;
}

static uint32_t dyn_get_pixel(const dyn_canvas_t *c, int x, int y) {
  if (x < 0 || x >= c->w || y < 0 || y >= c->h)
    return MAKE_COLOR(0, 0, 0, 0);
  const uint8_t *p = c->pixels + ((size_t)y * c->w + x) * 4;
  return MAKE_COLOR(p[0], p[1], p[2], p[3]);
}

static void dyn_set_pixel(dyn_canvas_t *c, int x, int y, uint32_t col) {
  if (x < 0 || x >= c->w || y < 0 || y >= c->h) return;
  uint8_t *p = c->pixels + ((size_t)y * c->w + x) * 4;
  p[0]=COLOR_R(col); p[1]=COLOR_G(col); p[2]=COLOR_B(col); p[3]=COLOR_A(col);
}

// Returns true on success, false on alloc failure (canvas unchanged).
static bool t_resize(dyn_canvas_t *c, int new_w, int new_h) {
  if (!c || new_w <= 0 || new_h <= 0) return false;
  if ((size_t)new_w > 16384 || (size_t)new_h > 16384) return false;
  uint8_t *buf = malloc((size_t)new_w * new_h * 4);
  if (!buf) return false;
  memset(buf, 0xFF, (size_t)new_w * new_h * 4);
  int copy_w = new_w < c->w ? new_w : c->w;
  int copy_h = new_h < c->h ? new_h : c->h;
  for (int y = 0; y < copy_h; y++)
    memcpy(buf + (size_t)y * new_w * 4,
           c->pixels + (size_t)y * c->w * 4,
           (size_t)copy_w * 4);
  free(c->pixels);
  c->pixels = buf;
  c->w = new_w; c->h = new_h;
  c->dirty = true;
  return true;
}

static void dyn_canvas_free(dyn_canvas_t *c) {
  if (c) { free(c->pixels); free(c); }
}



void test_color_eq(void) {
  TEST("MAKE_COLOR – matching colors are equal");
  uint32_t a = MAKE_COLOR(255,0,128,255);
  uint32_t b = MAKE_COLOR(255,0,128,255);
  ASSERT_TRUE(a == b);
  PASS();
}

void test_rgba_neq(void) {
  TEST("MAKE_COLOR – differing colors are not equal");
  uint32_t a = MAKE_COLOR(255,0,0,255);
  uint32_t b = MAKE_COLOR(0,255,0,255);
  ASSERT_FALSE(a == b);
  PASS();
}

void test_rgba_to_col_white(void) {
  TEST("MAKE_COLOR – white = 0xFFFFFFFF");
  uint32_t w = MAKE_COLOR(255,255,255,255);
  ASSERT_EQUAL(w, 0xFFFFFFFFu);
  PASS();
}

void test_rgba_to_col_black(void) {
  TEST("MAKE_COLOR – black = 0xFF000000");
  uint32_t b = MAKE_COLOR(0,0,0,255);
  ASSERT_EQUAL(b, 0xFF000000u);
  PASS();
}

void test_rgba_to_col_red(void) {
  TEST("MAKE_COLOR – red: R in LSB, A in MSB");
  uint32_t r = MAKE_COLOR(255,0,0,255);
  // Expected: AA=0xFF, BB=0x00, GG=0x00, RR=0xFF → 0xFF0000FF
  ASSERT_EQUAL(r, 0xFF0000FFu);
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
  uint32_t p = canvas_get_pixel(c, 0, 0);
  ASSERT_EQUAL(COLOR_R(p), 255);
  ASSERT_EQUAL(COLOR_G(p), 255);
  ASSERT_EQUAL(COLOR_B(p), 255);
  ASSERT_EQUAL(COLOR_A(p), 255);
  // Also check a pixel in the interior
  uint32_t q = canvas_get_pixel(c, 160, 100);
  ASSERT_TRUE(p == q);
  ASSERT_TRUE(c->canvas_dirty);
  free(c);
  PASS();
}

void test_set_get_pixel(void) {
  TEST("canvas_set_pixel / canvas_get_pixel – round-trip");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t col = MAKE_COLOR(10, 20, 30, 255);
  canvas_set_pixel(c, 5, 7, col);
  uint32_t got = canvas_get_pixel(c, 5, 7);
  ASSERT_TRUE(col == got);
  // Neighbouring pixel must be unchanged (white)
  uint32_t nbr = canvas_get_pixel(c, 6, 7);
  ASSERT_EQUAL(COLOR_R(nbr), 255);
  free(c);
  PASS();
}

void test_set_pixel_out_of_bounds(void) {
  TEST("canvas_set_pixel – out-of-bounds is silently ignored");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  c->canvas_dirty = false;
  canvas_set_pixel(c, -1, 0, MAKE_COLOR(0,0,0,255));
  canvas_set_pixel(c, 0, CANVAS_H, MAKE_COLOR(0,0,0,255));
  // dirty flag must not have changed
  ASSERT_FALSE(c->canvas_dirty);
  free(c);
  PASS();
}

void test_canvas_draw_circle(void) {
  TEST("canvas_draw_circle – centre pixel is coloured");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t red = MAKE_COLOR(255,0,0,255);
  canvas_draw_circle(c, 50, 50, 3, red);
  // Centre must be red
  ASSERT_TRUE(canvas_get_pixel(c, 50, 50) == red);
  // Pixel far away must still be white
  uint32_t far_pix = canvas_get_pixel(c, 100, 100);
  ASSERT_EQUAL(COLOR_R(far_pix), 255);
  ASSERT_EQUAL(COLOR_G(far_pix), 255);
  free(c);
  PASS();
}

void test_canvas_draw_line_horizontal(void) {
  TEST("canvas_draw_line – horizontal line");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t blue = MAKE_COLOR(0,0,255,255);
  canvas_draw_line(c, 10, 10, 20, 10, blue, 0);
  // Every pixel from x=10 to x=20 at y=10 must be blue
  for (int x = 10; x <= 20; x++) {
    ASSERT_TRUE(canvas_get_pixel(c, x, 10) == blue);
  }
  free(c);
  PASS();
}

void test_canvas_draw_line_single_point(void) {
  TEST("canvas_draw_line – start==end draws a single pixel");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t grn = MAKE_COLOR(0,255,0,255);
  canvas_draw_line(c, 30, 40, 30, 40, grn, 0);
  ASSERT_TRUE(canvas_get_pixel(c, 30, 40) == grn);
  free(c);
  PASS();
}

void test_flood_fill_simple(void) {
  TEST("canvas_flood_fill – fills contiguous region");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c); // all white

  // Draw a black border box 10x10 at (20,20)
  uint32_t blk = MAKE_COLOR(0,0,0,255);
  for (int x=20;x<30;x++) { canvas_set_pixel(c,x,20,blk); canvas_set_pixel(c,x,29,blk); }
  for (int y=20;y<30;y++) { canvas_set_pixel(c,20,y,blk); canvas_set_pixel(c,29,y,blk); }

  // Fill interior with red
  uint32_t red = MAKE_COLOR(255,0,0,255);
  canvas_flood_fill(c, 25, 25, red);

  // All interior pixels must be red
  ASSERT_TRUE(canvas_get_pixel(c, 25, 25) == red);
  ASSERT_TRUE(canvas_get_pixel(c, 21, 21) == red);

  // Border pixels must remain black
  ASSERT_TRUE(canvas_get_pixel(c, 20, 20) == blk);
  ASSERT_TRUE(canvas_get_pixel(c, 29, 20) == blk);

  // Pixel outside the box must remain white
  uint32_t out = canvas_get_pixel(c, 10, 10);
  ASSERT_EQUAL(COLOR_R(out), 255);
  free(c);
  PASS();
}

void test_flood_fill_same_color(void) {
  TEST("canvas_flood_fill – no-op when fill == target");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  c->canvas_dirty = false;
  uint32_t white = MAKE_COLOR(255,255,255,255);
  canvas_flood_fill(c, 0, 0, white); // fill white on white
  // dirty flag must not be set
  ASSERT_FALSE(c->canvas_dirty);
  free(c);
  PASS();
}

void test_is_png_valid(void) {
  TEST("is_png – file with PNG magic number returns true");
  // Write the 4-byte PNG signature to a temp file.
  const unsigned char png_hdr[4] = { 0x89, 0x50, 0x4E, 0x47 };
  char path[512];
  snprintf(path, sizeof(path), "%s/orion_is_png_valid_%d.bin",
           is_png_temp_dir(), (int)getpid());
  FILE *f = fopen(path, "wb");
  ASSERT_NOT_NULL(f);
  ASSERT_TRUE(fwrite(png_hdr, 1, 4, f) == 4);
  fclose(f);
  ASSERT_TRUE(is_png(path));
  remove(path);
  PASS();
}

void test_is_png_invalid(void) {
  TEST("is_png – non-PNG files and bad paths return false");
  // Write a file with JPEG magic bytes instead of PNG magic.
  const unsigned char jpeg_hdr[4] = { 0xFF, 0xD8, 0xFF, 0xE0 };
  char path[512];
  snprintf(path, sizeof(path), "%s/orion_is_png_invalid_%d.bin",
           is_png_temp_dir(), (int)getpid());
  FILE *f = fopen(path, "wb");
  ASSERT_NOT_NULL(f);
  ASSERT_TRUE(fwrite(jpeg_hdr, 1, 4, f) == 4);
  fclose(f);
  ASSERT_FALSE(is_png(path));
  remove(path);
  // Non-existent file must return false.
  ASSERT_FALSE(is_png("/tmp/orion_no_such_file_orion_xyz.png"));
  // NULL and empty path must return false.
  ASSERT_FALSE(is_png(NULL));
  ASSERT_FALSE(is_png(""));
  PASS();
}

void test_canvas_pixel_count(void) {
  TEST("canvas buffer size matches CANVAS_W x CANVAS_H x 4");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  ASSERT_EQUAL((int)sizeof(c->pixels), CANVAS_W * CANVAS_H * 4);
  free(c);
  PASS();
}

void test_draw_thick_line(void) {
  TEST("canvas_draw_line with radius – thick horizontal stroke");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t col = MAKE_COLOR(50, 100, 150, 255);
  // Draw a thick horizontal line at y=100 with radius 2
  canvas_draw_line(c, 50, 100, 80, 100, col, 2);
  // Pixels within radius of the line must be coloured
  ASSERT_TRUE(canvas_get_pixel(c, 65, 100) == col); // on line
  ASSERT_TRUE(canvas_get_pixel(c, 65, 101) == col); // 1 px below
  ASSERT_TRUE(canvas_get_pixel(c, 65, 102) == col); // 2 px below (r=2)
  // Pixel further away must remain white
  uint32_t far = canvas_get_pixel(c, 65, 105);
  ASSERT_EQUAL(COLOR_R(far), 255);
  free(c);
  PASS();
}

void test_undo_basic(void) {
  TEST("undo – restores pixel to pre-draw state");
  test_doc_t *d = calloc(1, sizeof(test_doc_t));
  memset(d->pixels, 0xFF, kTestSnapSize); // white canvas

  // Save undo state, then paint a red pixel
  tdoc_push_undo(d);
  uint8_t *p = d->pixels + (10 * CANVAS_W + 20) * 4;
  p[0]=255; p[1]=0; p[2]=0; p[3]=255;

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

static void t_rgb_to_hsv(uint32_t c, float *h, float *s, float *v) {
  float r = COLOR_R(c) / 255.0f, g = COLOR_G(c) / 255.0f, b = COLOR_B(c) / 255.0f;
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

static uint32_t t_hsv_to_rgb(float h, float s, float v, uint8_t a) {
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
  return MAKE_COLOR((uint8_t)(r*255.0f),(uint8_t)(g*255.0f),(uint8_t)(b*255.0f),a);
}

void test_rgb_to_hsv_black(void) {
  TEST("rgb_to_hsv – black gives v=0, s=0");
  uint32_t black = MAKE_COLOR(0, 0, 0, 255);
  float h, s, v;
  t_rgb_to_hsv(black, &h, &s, &v);
  ASSERT_EQUAL((int)(v * 100 + 0.5f), 0);
  ASSERT_EQUAL((int)(s * 100 + 0.5f), 0);
  PASS();
}

void test_rgb_to_hsv_white(void) {
  TEST("rgb_to_hsv – white gives v=1, s=0");
  uint32_t white = MAKE_COLOR(255, 255, 255, 255);
  float h, s, v;
  t_rgb_to_hsv(white, &h, &s, &v);
  ASSERT_EQUAL((int)(v * 100 + 0.5f), 100);
  ASSERT_EQUAL((int)(s * 100 + 0.5f), 0);
  PASS();
}

void test_rgb_to_hsv_red(void) {
  TEST("rgb_to_hsv – pure red gives h=0, s=1, v=1");
  uint32_t red = MAKE_COLOR(255, 0, 0, 255);
  float h, s, v;
  t_rgb_to_hsv(red, &h, &s, &v);
  ASSERT_EQUAL((int)(h * 360 + 0.5f), 0);
  ASSERT_EQUAL((int)(s * 100 + 0.5f), 100);
  ASSERT_EQUAL((int)(v * 100 + 0.5f), 100);
  PASS();
}

void test_hsv_to_rgb_round_trip(void) {
  TEST("hsv_to_rgb – RGB→HSV→RGB round-trip within 1 LSB");
  uint32_t colours[] = {
    MAKE_COLOR(128, 64, 200, 255), MAKE_COLOR(0, 200, 100, 255),
    MAKE_COLOR(255, 128, 0, 255),  MAKE_COLOR(10, 10, 10, 255)
  };
  for (int i = 0; i < 4; i++) {
    uint32_t c = colours[i];
    float h, s, v;
    t_rgb_to_hsv(c, &h, &s, &v);
    uint32_t back = t_hsv_to_rgb(h, s, v, COLOR_A(c));
    ASSERT_TRUE(abs((int)COLOR_R(back) - (int)COLOR_R(c)) <= 1);
    ASSERT_TRUE(abs((int)COLOR_G(back) - (int)COLOR_G(c)) <= 1);
    ASSERT_TRUE(abs((int)COLOR_B(back) - (int)COLOR_B(c)) <= 1);
  }
  PASS();
}

void test_hsv_to_rgb_gray(void) {
  TEST("hsv_to_rgb – s=0 produces gray (r==g==b)");
  uint32_t gray = t_hsv_to_rgb(0.0f, 0.0f, 0.5f, 255);
  ASSERT_EQUAL(COLOR_R(gray), COLOR_G(gray));
  ASSERT_EQUAL(COLOR_G(gray), COLOR_B(gray));
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

  uint32_t red = MAKE_COLOR(255, 0, 0, 255);
  uint32_t white = MAKE_COLOR(255, 255, 255, 255);

  /* Inside selection: should be written */
  canvas_set_pixel(c, 55, 55, red);
  ASSERT_TRUE(canvas_get_pixel(c, 55, 55) == red);

  /* Outside selection: must remain white */
  canvas_set_pixel(c, 40, 40, red);
  ASSERT_TRUE(canvas_get_pixel(c, 40, 40) == white);

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

  uint32_t blue = MAKE_COLOR(0, 0, 255, 255);
  uint32_t white = MAKE_COLOR(255, 255, 255, 255);
  canvas_flood_fill(c, 0, 0, blue);

  /* Pixel inside selection must be filled */
  ASSERT_TRUE(canvas_get_pixel(c, 80, 100) == blue);

  /* Pixel outside selection (right half) must remain white */
  ASSERT_TRUE(canvas_get_pixel(c, 200, 100) == white);

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

  uint32_t blue = MAKE_COLOR(0, 0, 255, 255);
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

  uint32_t red = MAKE_COLOR(255, 0, 0, 255);
  uint32_t white = MAKE_COLOR(255, 255, 255, 255);

  /* Inside the normalised [50,60]x[50,60] region */
  canvas_set_pixel(c, 55, 55, red);
  ASSERT_TRUE(canvas_get_pixel(c, 55, 55) == red);

  /* Outside — must remain white */
  canvas_set_pixel(c, 40, 40, red);
  ASSERT_TRUE(canvas_get_pixel(c, 40, 40) == white);

  free(c);
  PASS();
}

// ============================================================
// Shape drawing helper replicas
// (user stories: draw rectangles, ellipses, polygons, spray)
// ============================================================

/* Minimal point type used by polygon tests */
typedef struct { int x, y; } t_point_t;

/* Replicate canvas_draw_rect_outline from canvas.c */
static void t_canvas_draw_rect_outline(test_canvas_t *s, int x, int y,
                                        int w, int h, uint32_t c) {
  if (w <= 0 || h <= 0) return;
  canvas_draw_line(s, x,     y,     x+w-1, y,     c, 0);
  canvas_draw_line(s, x,     y+h-1, x+w-1, y+h-1, c, 0);
  canvas_draw_line(s, x,     y,     x,     y+h-1, c, 0);
  canvas_draw_line(s, x+w-1, y,     x+w-1, y+h-1, c, 0);
}

/* Replicate canvas_draw_rect_filled from canvas.c */
static void t_canvas_draw_rect_filled(test_canvas_t *s, int x, int y,
                                       int w, int h,
                                       uint32_t outline, uint32_t fill) {
  if (w <= 0 || h <= 0) return;
  for (int dy = 1; dy < h - 1; dy++)
    canvas_draw_line(s, x+1, y+dy, x+w-2, y+dy, fill, 0);
  t_canvas_draw_rect_outline(s, x, y, w, h, outline);
}

/* Replicate canvas_draw_ellipse_outline from canvas.c (Bresenham midpoint) */
static void t_canvas_draw_ellipse_outline(test_canvas_t *s,
                                           int cx, int cy,
                                           int rx, int ry, uint32_t c) {
  if (rx <= 0 || ry <= 0) return;
  long rx2 = (long)rx*rx, ry2 = (long)ry*ry;
  long ex = 0, ey = ry;
  long dx2 = 2*ry2*ex, dy2 = 2*rx2*ey;
  long p = (long)(ry2 - rx2*ry + 0.25*rx2);
  while (dx2 < dy2) {
    canvas_set_pixel(s, (int)(cx+ex), (int)(cy+ey), c);
    canvas_set_pixel(s, (int)(cx-ex), (int)(cy+ey), c);
    canvas_set_pixel(s, (int)(cx+ex), (int)(cy-ey), c);
    canvas_set_pixel(s, (int)(cx-ex), (int)(cy-ey), c);
    ex++; dx2 += 2*ry2;
    if (p < 0) { p += ry2 + dx2; }
    else { ey--; dy2 -= 2*rx2; p += ry2 + dx2 - dy2; }
  }
  p = (long)(ry2*(ex+0.5)*(ex+0.5) + rx2*(ey-1)*(ey-1) - rx2*ry2);
  while (ey >= 0) {
    canvas_set_pixel(s, (int)(cx+ex), (int)(cy+ey), c);
    canvas_set_pixel(s, (int)(cx-ex), (int)(cy+ey), c);
    canvas_set_pixel(s, (int)(cx+ex), (int)(cy-ey), c);
    canvas_set_pixel(s, (int)(cx-ex), (int)(cy-ey), c);
    ey--; dy2 -= 2*rx2;
    if (p > 0) { p += rx2 - dy2; }
    else { ex++; dx2 += 2*ry2; p += rx2 - dy2 + dx2; }
  }
}

/* Replicate canvas_draw_ellipse_filled from canvas.c */
static void t_canvas_draw_ellipse_filled(test_canvas_t *s,
                                          int cx, int cy,
                                          int rx, int ry,
                                          uint32_t outline, uint32_t fill) {
  if (rx <= 0 || ry <= 0) return;
  double rx2 = (double)rx*(double)rx;
  double ry2 = (double)ry*(double)ry;
  for (int py = cy - ry; py <= cy + ry; py++) {
    if (!canvas_in_bounds(cx, py)) continue;
    double ddy = (double)(py - cy);
    double t   = 1.0 - (ddy*ddy) / ry2;
    if (t <= 0.0) continue;
    int ddx = (int)(sqrt(rx2 * t) + 0.5);
    canvas_draw_line(s, cx-ddx+1, py, cx+ddx-1, py, fill, 0);
  }
  t_canvas_draw_ellipse_outline(s, cx, cy, rx, ry, outline);
}

/* Replicate canvas_draw_polygon_outline from canvas.c */
static void t_canvas_draw_polygon_outline(test_canvas_t *s,
                                           const t_point_t *pts, int count,
                                           uint32_t c) {
  if (count < 2) return;
  for (int i = 0; i < count - 1; i++)
    canvas_draw_line(s, pts[i].x, pts[i].y, pts[i+1].x, pts[i+1].y, c, 0);
  canvas_draw_line(s, pts[count-1].x, pts[count-1].y, pts[0].x, pts[0].y, c, 0);
}

/* Replicate canvas_spray from canvas.c */
static void t_canvas_spray(test_canvas_t *s, int cx, int cy,
                             int radius, uint32_t c) {
  int r2 = radius * radius;
  for (int i = 0; i < 20; i++) {
    int ddx = (rand() % (2 * radius + 1)) - radius;
    int ddy = (rand() % (2 * radius + 1)) - radius;
    if (ddx * ddx + ddy * ddy <= r2)
      canvas_set_pixel(s, cx + ddx, cy + ddy, c);
  }
}

/* Replicate canvas_is_shape_tool from canvas.c */
static bool t_canvas_is_shape_tool(int tool_id) {
  switch (tool_id) {
    case 27: /* ID_TOOL_LINE */
    case 28: /* ID_TOOL_RECT */
    case 29: /* ID_TOOL_ELLIPSE */
    case 30: /* ID_TOOL_ROUNDED_RECT */
      return true;
    default:
      return false;
  }
}

// ============================================================
// User Story: Draw a rectangle to frame an area of the canvas
// ============================================================

void test_draw_rect_outline(void) {
  TEST("canvas_draw_rect_outline – all four edges are coloured");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t red = MAKE_COLOR(255, 0, 0, 255);
  /* 30x15 rect with top-left at (10, 20) */
  t_canvas_draw_rect_outline(c, 10, 20, 30, 15, red);
  /* All four corners must be red */
  ASSERT_TRUE(canvas_get_pixel(c, 10, 20) == red);  /* top-left */
  ASSERT_TRUE(canvas_get_pixel(c, 39, 20) == red);  /* top-right  x+w-1 */
  ASSERT_TRUE(canvas_get_pixel(c, 10, 34) == red);  /* bot-left   y+h-1 */
  ASSERT_TRUE(canvas_get_pixel(c, 39, 34) == red);  /* bot-right */
  /* Interior must remain white */
  uint32_t interior = canvas_get_pixel(c, 20, 25);
  ASSERT_EQUAL(COLOR_R(interior), 255);
  ASSERT_EQUAL(COLOR_G(interior), 255);
  ASSERT_EQUAL(COLOR_B(interior), 255);
  free(c);
  PASS();
}

void test_draw_rect_outline_zero_size(void) {
  TEST("canvas_draw_rect_outline – zero-size rect is a no-op");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  c->canvas_dirty = false;
  uint32_t red = MAKE_COLOR(255, 0, 0, 255);
  t_canvas_draw_rect_outline(c, 10, 10, 0, 5, red);
  t_canvas_draw_rect_outline(c, 10, 10, 5, 0, red);
  ASSERT_FALSE(c->canvas_dirty);
  free(c);
  PASS();
}

// ============================================================
// User Story: Fill a rectangular area with a flat color
// ============================================================

void test_draw_rect_filled(void) {
  TEST("canvas_draw_rect_filled – interior receives fill color, border uses outline");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t outline = MAKE_COLOR(0, 0, 0, 255);
  uint32_t fill    = MAKE_COLOR(0, 128, 255, 255);
  t_canvas_draw_rect_filled(c, 10, 10, 20, 10, outline, fill);
  /* An interior pixel should have the fill color */
  ASSERT_TRUE(canvas_get_pixel(c, 15, 14) == fill);
  /* The border pixel should have the outline color */
  ASSERT_TRUE(canvas_get_pixel(c, 10, 10) == outline);
  /* A pixel outside the rect must remain white */
  uint32_t outside = canvas_get_pixel(c, 5, 5);
  ASSERT_EQUAL(COLOR_R(outside), 255);
  free(c);
  PASS();
}

// ============================================================
// User Story: Draw an oval/circle shape on the canvas
// ============================================================

void test_draw_ellipse_outline(void) {
  TEST("canvas_draw_ellipse_outline – topmost and bottommost ellipse points are coloured");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t blue = MAKE_COLOR(0, 0, 255, 255);
  int cx = 80, cy = 60, rx = 20, ry = 10;
  t_canvas_draw_ellipse_outline(c, cx, cy, rx, ry, blue);
  /* Topmost point: (cx, cy-ry) */
  ASSERT_TRUE(canvas_get_pixel(c, cx, cy - ry) == blue);
  /* Bottommost point: (cx, cy+ry) */
  ASSERT_TRUE(canvas_get_pixel(c, cx, cy + ry) == blue);
  /* Center should remain white (outline only) */
  uint32_t center = canvas_get_pixel(c, cx, cy);
  ASSERT_EQUAL(COLOR_R(center), 255);
  ASSERT_EQUAL(COLOR_G(center), 255);
  free(c);
  PASS();
}

void test_draw_ellipse_filled(void) {
  TEST("canvas_draw_ellipse_filled – center pixel receives fill color");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t outline = MAKE_COLOR(0, 0, 0, 255);
  uint32_t fill    = MAKE_COLOR(255, 200, 0, 255);
  t_canvas_draw_ellipse_filled(c, 100, 80, 15, 8, outline, fill);
  /* Center must be filled */
  ASSERT_TRUE(canvas_get_pixel(c, 100, 80) == fill);
  /* Point well outside the ellipse must remain white */
  uint32_t far = canvas_get_pixel(c, 160, 80);
  ASSERT_EQUAL(COLOR_R(far), 255);
  ASSERT_EQUAL(COLOR_G(far), 255);
  free(c);
  PASS();
}

void test_draw_ellipse_circle(void) {
  TEST("canvas_draw_ellipse_outline – rx==ry produces a circle (leftmost point coloured)");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t grn = MAKE_COLOR(0, 255, 0, 255);
  int cx = 50, cy = 50, r = 15;
  t_canvas_draw_ellipse_outline(c, cx, cy, r, r, grn);
  /* Leftmost and rightmost points */
  ASSERT_TRUE(canvas_get_pixel(c, cx - r, cy) == grn);
  ASSERT_TRUE(canvas_get_pixel(c, cx + r, cy) == grn);
  free(c);
  PASS();
}

// ============================================================
// User Story: Draw a custom polygon by clicking multiple points
// ============================================================

void test_draw_polygon_triangle(void) {
  TEST("canvas_draw_polygon_outline – all three vertices of a triangle are coloured");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t grn = MAKE_COLOR(0, 255, 0, 255);
  t_point_t tri[] = {{50, 10}, {90, 50}, {10, 50}};
  t_canvas_draw_polygon_outline(c, tri, 3, grn);
  /* Each vertex must be coloured */
  ASSERT_TRUE(canvas_get_pixel(c, 50, 10) == grn);
  ASSERT_TRUE(canvas_get_pixel(c, 90, 50) == grn);
  ASSERT_TRUE(canvas_get_pixel(c, 10, 50) == grn);
  free(c);
  PASS();
}

void test_draw_polygon_single_edge(void) {
  TEST("canvas_draw_polygon_outline – two-point polygon draws one closed edge");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t red = MAKE_COLOR(255, 0, 0, 255);
  t_point_t seg[] = {{10, 10}, {20, 10}};
  t_canvas_draw_polygon_outline(c, seg, 2, red);
  /* Mid-point of the only horizontal edge */
  ASSERT_TRUE(canvas_get_pixel(c, 15, 10) == red);
  free(c);
  PASS();
}

// ============================================================
// User Story: Apply soft spray-paint airbrush texture
// ============================================================

void test_spray_deposits_pixels(void) {
  TEST("canvas_spray – deposits pixels within the spray radius");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t red = MAKE_COLOR(255, 0, 0, 255);
  srand(42);
  /* Apply many spray strokes to guarantee coverage */
  for (int i = 0; i < 50; i++)
    t_canvas_spray(c, 100, 100, 10, red);
  /* Count coloured pixels inside the spray radius */
  int colored = 0;
  for (int ddy = -10; ddy <= 10; ddy++)
    for (int ddx = -10; ddx <= 10; ddx++)
      if (ddx*ddx + ddy*ddy <= 100) {
        uint32_t px = canvas_get_pixel(c, 100+ddx, 100+ddy);
        if (COLOR_R(px) == 255 && COLOR_G(px) == 0)
          colored++;
      }
  ASSERT_TRUE(colored > 0);
  /* Pixels far outside the area must remain white; stay within canvas bounds */
  uint32_t far_pix = canvas_get_pixel(c, 150, 150);
  ASSERT_EQUAL(COLOR_R(far_pix), 255);
  ASSERT_EQUAL(COLOR_G(far_pix), 255);
  free(c);
  PASS();
}

void test_spray_respects_radius(void) {
  TEST("canvas_spray – never deposits pixels beyond its radius");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t red = MAKE_COLOR(255, 0, 0, 255);
  int cx = 50, cy = 50, radius = 5;
  srand(7);
  for (int i = 0; i < 200; i++)
    t_canvas_spray(c, cx, cy, radius, red);
  /* No pixel strictly outside the radius must be coloured */
  for (int ddy = -(radius+2); ddy <= (radius+2); ddy++) {
    for (int ddx = -(radius+2); ddx <= (radius+2); ddx++) {
      if (ddx*ddx + ddy*ddy > radius*radius) {
        uint32_t px = canvas_get_pixel(c, cx+ddx, cy+ddy);
        ASSERT_EQUAL(COLOR_R(px), 255);  /* should still be white */
        ASSERT_EQUAL(COLOR_G(px), 255);
        ASSERT_EQUAL(COLOR_B(px), 255);
      }
    }
  }
  free(c);
  PASS();
}

// ============================================================
// User Story: Know which tools use rubber-band drag preview
// ============================================================

void test_is_shape_tool(void) {
  TEST("canvas_is_shape_tool – shape tools return true, non-shape tools return false");
  /* Shape tools */
  ASSERT_TRUE(t_canvas_is_shape_tool(27));   /* ID_TOOL_LINE */
  ASSERT_TRUE(t_canvas_is_shape_tool(28));   /* ID_TOOL_RECT */
  ASSERT_TRUE(t_canvas_is_shape_tool(29));   /* ID_TOOL_ELLIPSE */
  ASSERT_TRUE(t_canvas_is_shape_tool(30));   /* ID_TOOL_ROUNDED_RECT */
  /* Non-shape tools */
  ASSERT_FALSE(t_canvas_is_shape_tool(20));  /* ID_TOOL_PENCIL */
  ASSERT_FALSE(t_canvas_is_shape_tool(21));  /* ID_TOOL_BRUSH */
  ASSERT_FALSE(t_canvas_is_shape_tool(22));  /* ID_TOOL_ERASER */
  ASSERT_FALSE(t_canvas_is_shape_tool(23));  /* ID_TOOL_FILL */
  ASSERT_FALSE(t_canvas_is_shape_tool(24));  /* ID_TOOL_SELECT */
  ASSERT_FALSE(t_canvas_is_shape_tool(33));  /* ID_TOOL_EYEDROPPER */
  ASSERT_FALSE(t_canvas_is_shape_tool(35));  /* ID_TOOL_TEXT */
  PASS();
}

// ============================================================
// User Story: Draw diagonal lines at arbitrary angles
// ============================================================

void test_canvas_draw_line_diagonal(void) {
  TEST("canvas_draw_line – diagonal line passes through start and end");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t col = MAKE_COLOR(200, 100, 50, 255);
  canvas_draw_line(c, 5, 5, 25, 25, col, 0);
  ASSERT_TRUE(canvas_get_pixel(c,  5,  5) == col);
  ASSERT_TRUE(canvas_get_pixel(c, 25, 25) == col);
  /* Midpoint must also be set for a 45° line */
  ASSERT_TRUE(canvas_get_pixel(c, 15, 15) == col);
  free(c);
  PASS();
}

// ============================================================
// User Story: Crop canvas to the active selection
// ============================================================

/* Replicate canvas_crop_to_selection from canvas.c.
 * Works against test_canvas_t whose sel_active / sel_x0…sel_y1 mirror
 * the production sel_active / sel_start / sel_end fields. */
static void t_canvas_crop_to_selection(test_canvas_t *s) {
  if (!s->sel_active) return;
  int x0 = s->sel_x0 < s->sel_x1 ? s->sel_x0 : s->sel_x1;
  int y0 = s->sel_y0 < s->sel_y1 ? s->sel_y0 : s->sel_y1;
  int x1 = s->sel_x0 > s->sel_x1 ? s->sel_x0 : s->sel_x1;
  int y1 = s->sel_y0 > s->sel_y1 ? s->sel_y0 : s->sel_y1;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 >= CANVAS_W) x1 = CANVAS_W - 1;
  if (y1 >= CANVAS_H) y1 = CANVAS_H - 1;
  if (x0 > x1 || y0 > y1) return;
  int w = x1 - x0 + 1;
  int h = y1 - y0 + 1;
  uint8_t *buf = malloc((size_t)w * h * 4);
  if (!buf) return;
  /* Copy selected region into temp buffer */
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      uint32_t c = canvas_get_pixel(s, x0 + col, y0 + row);
      uint8_t *p = buf + ((size_t)row * w + col) * 4;
      p[0] = COLOR_R(c); p[1] = COLOR_G(c); p[2] = COLOR_B(c); p[3] = COLOR_A(c);
    }
  }
  /* Fill entire canvas with white */
  canvas_clear(s);
  /* Deactivate selection before stamping so set_pixel bypasses the mask */
  s->sel_active = false;
  /* Stamp buffer at (0,0) */
  for (int row = 0; row < h && row < CANVAS_H; row++) {
    for (int col = 0; col < w && col < CANVAS_W; col++) {
      const uint8_t *p = buf + ((size_t)row * w + col) * 4;
      uint32_t c = MAKE_COLOR(p[0], p[1], p[2], p[3]);
      canvas_set_pixel(s, col, row, c);
    }
  }
  free(buf);
  s->canvas_dirty = true;
}

void test_crop_to_selection_basic(void) {
  TEST("canvas_crop_to_selection – selected pixels appear at (0,0) after crop");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  /* Paint a distinct colour in a 10x10 block at (50,40) */
  uint32_t red = MAKE_COLOR(255, 0, 0, 255);
  for (int dy = 0; dy < 10; dy++)
    for (int dx = 0; dx < 10; dx++)
      canvas_set_pixel(c, 50 + dx, 40 + dy, red);
  /* Select exactly that block */
  c->sel_active = true;
  c->sel_x0 = 50; c->sel_y0 = 40;
  c->sel_x1 = 59; c->sel_y1 = 49;
  t_canvas_crop_to_selection(c);
  /* Top-left of canvas must now contain the red pixels */
  ASSERT_TRUE(canvas_get_pixel(c, 0, 0) == red);
  ASSERT_TRUE(canvas_get_pixel(c, 9, 9) == red);
  /* Selection must be cleared */
  ASSERT_FALSE(c->sel_active);
  /* Pixels beyond the cropped area must be white */
  uint32_t beyond = canvas_get_pixel(c, 10, 0);
  ASSERT_EQUAL(COLOR_R(beyond), 255);
  ASSERT_EQUAL(COLOR_G(beyond), 255);
  ASSERT_EQUAL(COLOR_B(beyond), 255);
  free(c);
  PASS();
}

void test_crop_to_selection_no_selection(void) {
  TEST("canvas_crop_to_selection – no-op when selection is inactive");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  /* Paint a marker pixel */
  uint32_t blue = MAKE_COLOR(0, 0, 255, 255);
  canvas_set_pixel(c, 100, 100, blue);
  c->sel_active = false;
  t_canvas_crop_to_selection(c);
  /* Canvas should be unchanged */
  ASSERT_TRUE(canvas_get_pixel(c, 100, 100) == blue);
  free(c);
  PASS();
}

void test_crop_to_selection_preserves_content(void) {
  TEST("canvas_crop_to_selection – content outside selection is discarded");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  /* Scatter different colours across the canvas */
  uint32_t red   = MAKE_COLOR(255, 0, 0, 255);
  uint32_t green = MAKE_COLOR(0, 255, 0, 255);
  canvas_set_pixel(c, 20, 20, red);    /* inside selection */
  canvas_set_pixel(c, 200, 100, green); /* outside selection */
  /* Select a 30x30 area around (10,10)–(39,39) */
  c->sel_active = true;
  c->sel_x0 = 10; c->sel_y0 = 10;
  c->sel_x1 = 39; c->sel_y1 = 39;
  t_canvas_crop_to_selection(c);
  /* The red pixel was at canvas (20,20). The selection top-left is (10,10),
   * so after crop it moves to offset (20-10, 20-10) = (10,10) in the new canvas. */
  ASSERT_TRUE(canvas_get_pixel(c, 10, 10) == red);
  /* The green pixel at (200,100) was outside the selection and must have been
   * discarded (entire canvas filled white, only the cropped region placed at origin).
   * Check a point well outside the 30x30 cropped area that must be white. */
  uint32_t gone = canvas_get_pixel(c, 100, 80);
  ASSERT_EQUAL(COLOR_R(gone), 255);
  ASSERT_EQUAL(COLOR_G(gone), 255);
  free(c);
  PASS();
}

void test_crop_to_selection_reversed_drag(void) {
  TEST("canvas_crop_to_selection – reversed selection (right-to-left drag) is normalised");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t col = MAKE_COLOR(128, 64, 192, 255);
  canvas_set_pixel(c, 30, 30, col);
  /* Reversed drag: x1 < x0, y1 < y0 */
  c->sel_active = true;
  c->sel_x0 = 40; c->sel_y0 = 40;
  c->sel_x1 = 20; c->sel_y1 = 20;
  t_canvas_crop_to_selection(c);
  /* Pixel at (30,30) was 10px from top-left of normalised selection (20,20) */
  ASSERT_TRUE(canvas_get_pixel(c, 10, 10) == col);
  ASSERT_FALSE(c->sel_active);
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

/* Scrollbar thickness – must match SCROLLBAR_SIZE in imageeditor.h */
#define T_SCROLLBAR_SIZE 8

/* Replicate clamp_pan from win_canvas.c.
 *
 * The horizontal scrollbar now lives on the document window (merged with the
 * status bar), so it no longer reduces the canvas viewport height.  Only the
 * vertical scrollbar is inside the canvas, always consuming T_SCROLLBAR_SIZE
 * pixels on the right (CANVAS_SB_ALWAYS_VISIBLE mode). */
static void t_clamp_pan(int scale, int win_w, int win_h,
                         int *pan_x, int *pan_y) {
  int canvas_w = CANVAS_W * scale;
  int canvas_h = CANVAS_H * scale;

  /* vscroll always takes the right strip; hscroll is external (no height cost) */
  int view_w = win_w - T_SCROLLBAR_SIZE;
  int view_h = win_h;

  int max_x = canvas_w - view_w;
  int max_y = canvas_h - view_h;
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
  TEST("pan clamp – 1x zoom: vscroll always takes right strip; vertical fits");
  /* At 1x zoom with win == canvas size:
   *   view_w = CANVAS_W - T_SCROLLBAR_SIZE  →  max_pan_x = T_SCROLLBAR_SIZE
   *   view_h = CANVAS_H                      →  max_pan_y = 0               */
  int px = 100, py = 50;
  t_clamp_pan(1, CANVAS_W, CANVAS_H, &px, &py);
  ASSERT_EQUAL(px, T_SCROLLBAR_SIZE);
  ASSERT_EQUAL(py, 0);
  PASS();
}

void test_pan_clamp_zoom_4x(void) {
  TEST("pan clamp – 4x zoom: pan stays within valid range");
  /* At 4x zoom with win size == canvas size.
   * vscroll consumes T_SCROLLBAR_SIZE on the right; hscroll is external:
   *   max_pan_x = canvas_w*4 - (win_w - T_SCROLLBAR_SIZE)
   *             = 4*320 - (320-8) = 1280 - 312 = 968
   *   max_pan_y = canvas_h*4 - win_h
   *             = 4*200 - 200 = 800 - 200 = 600               */
  int px = 9999, py = 9999;
  t_clamp_pan(4, CANVAS_W, CANVAS_H, &px, &py);
  ASSERT_EQUAL(px, CANVAS_W * 4 - (CANVAS_W - T_SCROLLBAR_SIZE));  /* 968 */
  ASSERT_EQUAL(py, CANVAS_H * 4 - CANVAS_H);                        /* 600 */
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
  /* 2x zoom, window size = CANVAS size.
   * max_pan_x = 2*320 - (320-8) = 640-312 = 328 > 100  ✓
   * max_pan_y = 2*200 - 200     = 400-200 = 200 > 80   ✓ */
  t_clamp_pan(2, CANVAS_W, CANVAS_H, &px, &py);
  ASSERT_EQUAL(px, 100);
  ASSERT_EQUAL(py, 80);
  PASS();
}

void test_pan_clamp_vscroll_width_cost(void) {
  TEST("pan clamp – vscroll always reduces horizontal viewport by T_SCROLLBAR_SIZE");
  /* Canvas width exactly equals window width at 1x.  The vscroll always
   * occupies T_SCROLLBAR_SIZE pixels on the right, so there is always a
   * horizontal overflow of T_SCROLLBAR_SIZE.  max_pan_x = T_SCROLLBAR_SIZE. */
  int win_w = CANVAS_W, win_h = CANVAS_H;
  int px = 999, py = 999;
  t_clamp_pan(1, win_w, win_h, &px, &py);
  ASSERT_EQUAL(px, T_SCROLLBAR_SIZE);
  ASSERT_EQUAL(py, 0);
  PASS();
}

void test_pan_clamp_no_height_cost_for_hscroll(void) {
  TEST("pan clamp – hscroll (on doc window) does not reduce canvas viewport height");
  /* Canvas height overflows at 4x.  Because hscroll is external (merged with
   * the doc-window status bar), view_h = win_h (no 8px eaten at the bottom).
   * max_pan_y = 4*CANVAS_H - CANVAS_H = 3*CANVAS_H. */
  int win_w = CANVAS_W, win_h = CANVAS_H;
  int px = 0, py = 9999;
  t_clamp_pan(4, win_w, win_h, &px, &py);
  ASSERT_EQUAL(py, CANVAS_H * 3);
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
static void t_set_pixel_direct(test_canvas_t *s, int x, int y, uint32_t c) {
  if (!canvas_in_bounds(x, y)) return;
  uint8_t *p = s->pixels + ((size_t)y * CANVAS_W + x) * 4;
  p[0]=COLOR_R(c); p[1]=COLOR_G(c); p[2]=COLOR_B(c); p[3]=COLOR_A(c);
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
      uint32_t c = canvas_get_pixel(s, x0 + col, y0 + row);
      uint8_t *p = out + ((size_t)row * (*out_w) + col) * 4;
      p[0]=COLOR_R(c); p[1]=COLOR_G(c); p[2]=COLOR_B(c); p[3]=COLOR_A(c);
    }
  }
}

// Fill the selected region with fill_color (respects selection mask).
static void t_clear_selection(test_canvas_t *s, uint32_t fill) {
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
      t_set_pixel_direct(s, dx + col, dy + row, MAKE_COLOR(p[0], p[1], p[2], p[3]));
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
  /* Paint a red 3x3 block at (10,10)..(12,12) */
  uint32_t red = MAKE_COLOR(255, 0, 0, 255);
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
    uint32_t got = MAKE_COLOR(buf[i*4+0], buf[i*4+1], buf[i*4+2], buf[i*4+3]);
    ASSERT_TRUE(got == red);
  }
  free(buf);
  free(c);
  PASS();
}

void test_clear_selection_fills_bg(void) {
  TEST("canvas_clear_selection – fills selected region with background color");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);  /* white */
  uint32_t red  = MAKE_COLOR(255, 0, 0, 255);
  uint32_t blue = MAKE_COLOR(0, 0, 255, 255);
  uint32_t white = MAKE_COLOR(255, 255, 255, 255);
  /* Paint a red block at (5,5)..(9,9) */
  for (int y = 5; y <= 9; y++)
    for (int x = 5; x <= 9; x++)
      t_set_pixel_direct(c, x, y, red);
  /* Select and clear with blue */
  c->sel_active = true;
  c->sel_x0 = 5; c->sel_y0 = 5; c->sel_x1 = 9; c->sel_y1 = 9;
  t_clear_selection(c, blue);
  /* Inside selection must now be blue */
  ASSERT_TRUE(canvas_get_pixel(c, 7, 7) == blue);
  /* Pixel just outside selection must remain white */
  ASSERT_TRUE(canvas_get_pixel(c, 4, 7) == white);
  ASSERT_TRUE(canvas_get_pixel(c, 10, 7) == white);
  free(c);
  PASS();
}

void test_paste_pixels_at_offset(void) {
  TEST("t_paste_pixels – places clipboard pixels at destination offset");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);  /* white */
  uint32_t green = MAKE_COLOR(0, 255, 0, 255);
  uint32_t white = MAKE_COLOR(255, 255, 255, 255);
  /* Clipboard: 2x2 green block */
  uint8_t src[16];
  for (int i = 0; i < 4; i++) {
    src[i*4+0]=COLOR_R(green); src[i*4+1]=COLOR_G(green);
    src[i*4+2]=COLOR_B(green); src[i*4+3]=COLOR_A(green);
  }
  t_paste_pixels(c, src, 2, 2, 100, 50);
  ASSERT_TRUE(canvas_get_pixel(c, 100, 50) == green);
  ASSERT_TRUE(canvas_get_pixel(c, 101, 51) == green);
  /* Adjacent pixels outside paste region must be white */
  ASSERT_TRUE(canvas_get_pixel(c, 99, 50) == white);
  ASSERT_TRUE(canvas_get_pixel(c, 102, 50) == white);
  free(c);
  PASS();
}

void test_cut_selection_clears_and_copies(void) {
  TEST("canvas_cut_selection – copies pixels to buffer and clears region");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t red  = MAKE_COLOR(255, 0, 0, 255);
  uint32_t white = MAKE_COLOR(255, 255, 255, 255);
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
    uint32_t got = MAKE_COLOR(buf[i*4+0], buf[i*4+1], buf[i*4+2], buf[i*4+3]);
    ASSERT_TRUE(got == red);
  }
  /* Canvas must now be white in that region */
  ASSERT_TRUE(canvas_get_pixel(c, 21, 21) == white);
  free(buf);
  free(c);
  PASS();
}

void test_move_selection(void) {
  TEST("canvas_begin/commit_move – moves selected pixels to new position");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);  /* white */
  uint32_t red   = MAKE_COLOR(255, 0, 0, 255);
  uint32_t white = MAKE_COLOR(255, 255, 255, 255);
  /* Place a red 3x3 block at (10,10) */
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
  ASSERT_TRUE(canvas_get_pixel(c, 11, 11) == white);
  /* New location must be red */
  ASSERT_TRUE(canvas_get_pixel(c, 51, 51) == red);
  free(c);
  PASS();
}

void test_paste_respects_oob(void) {
  TEST("t_paste_pixels – clips pixels that fall outside canvas bounds");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t red = MAKE_COLOR(255, 0, 0, 255);
  /* 4x4 clipboard */
  uint8_t src[64];
  for (int i = 0; i < 16; i++) {
    src[i*4+0]=COLOR_R(red); src[i*4+1]=COLOR_G(red); src[i*4+2]=COLOR_B(red); src[i*4+3]=COLOR_A(red);
  }
  /* Paste partially off the right/bottom edge */
  t_paste_pixels(c, src, 4, 4, CANVAS_W - 2, CANVAS_H - 2);
  /* In-bounds pixels must be red */
  ASSERT_TRUE(canvas_get_pixel(c, CANVAS_W-2, CANVAS_H-2) == red);
  ASSERT_TRUE(canvas_get_pixel(c, CANVAS_W-1, CANVAS_H-1) == red);
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

// ============================================================
// canvas_flip_h tests
// ============================================================

void test_flip_h_mirrors_pixels(void) {
  TEST("canvas_flip_h – leftmost pixel becomes rightmost after flip");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t red = MAKE_COLOR(255, 0, 0, 255);
  t_set_pixel_direct(c, 0, 0, red);
  t_flip_h(c);
  ASSERT_TRUE(canvas_get_pixel(c, CANVAS_W - 1, 0) == red);
  ASSERT_FALSE(canvas_get_pixel(c, 0, 0) == red);
  free(c);
  PASS();
}

void test_flip_h_even_width(void) {
  TEST("canvas_flip_h – column 1 maps to column W-2 (even-width check)");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t blue = MAKE_COLOR(0, 0, 255, 255);
  t_set_pixel_direct(c, 1, 5, blue);
  t_flip_h(c);
  ASSERT_TRUE(canvas_get_pixel(c, CANVAS_W - 2, 5) == blue);
  free(c);
  PASS();
}

void test_flip_h_single_row(void) {
  TEST("canvas_flip_h – only the targeted row is modified");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t green = MAKE_COLOR(0, 255, 0, 255);
  uint32_t white = MAKE_COLOR(255, 255, 255, 255);
  t_set_pixel_direct(c, 0, 3, green);   /* mark row 3 col 0 */
  t_flip_h(c);
  ASSERT_TRUE(canvas_get_pixel(c, CANVAS_W - 1, 3) == green);
  /* Row 4 should still be white (unaffected) */
  ASSERT_TRUE(canvas_get_pixel(c, CANVAS_W - 1, 4) == white);
  free(c);
  PASS();
}

void test_flip_h_double_is_identity(void) {
  TEST("canvas_flip_h – applied twice restores original pixels");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t red = MAKE_COLOR(200, 100, 50, 255);
  t_set_pixel_direct(c, 10, 20, red);
  t_flip_h(c);
  t_flip_h(c);
  ASSERT_TRUE(canvas_get_pixel(c, 10, 20) == red);
  ASSERT_TRUE(canvas_get_pixel(c, CANVAS_W - 1 - 10, 20) ==
              MAKE_COLOR(255, 255, 255, 255)); /* restored to white */
  free(c);
  PASS();
}

// ============================================================
// canvas_flip_v tests
// ============================================================

void test_flip_v_mirrors_rows(void) {
  TEST("canvas_flip_v – top row becomes bottom row after flip");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t red = MAKE_COLOR(255, 0, 0, 255);
  t_set_pixel_direct(c, 0, 0, red);
  t_flip_v(c);
  ASSERT_TRUE(canvas_get_pixel(c, 0, CANVAS_H - 1) == red);
  ASSERT_FALSE(canvas_get_pixel(c, 0, 0) == red);
  free(c);
  PASS();
}

void test_flip_v_even_height(void) {
  TEST("canvas_flip_v – row 1 maps to row H-2");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t blue = MAKE_COLOR(0, 0, 255, 255);
  t_set_pixel_direct(c, 5, 1, blue);
  t_flip_v(c);
  ASSERT_TRUE(canvas_get_pixel(c, 5, CANVAS_H - 2) == blue);
  free(c);
  PASS();
}

void test_flip_v_single_col(void) {
  TEST("canvas_flip_v – only the target row is modified");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t green = MAKE_COLOR(0, 255, 0, 255);
  uint32_t white = MAKE_COLOR(255, 255, 255, 255);
  t_set_pixel_direct(c, 7, 0, green);  /* row 0, col 7 */
  t_flip_v(c);
  ASSERT_TRUE(canvas_get_pixel(c, 7, CANVAS_H - 1) == green);
  /* Other columns in the bottom row should be white */
  ASSERT_TRUE(canvas_get_pixel(c, 8, CANVAS_H - 1) == white);
  free(c);
  PASS();
}

void test_flip_v_double_is_identity(void) {
  TEST("canvas_flip_v – applied twice restores original pixels");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t red = MAKE_COLOR(100, 200, 50, 255);
  t_set_pixel_direct(c, 15, 10, red);
  t_flip_v(c);
  t_flip_v(c);
  ASSERT_TRUE(canvas_get_pixel(c, 15, 10) == red);
  free(c);
  PASS();
}

// ============================================================
// canvas_invert_colors tests
// ============================================================

void test_invert_colors_complement(void) {
  TEST("canvas_invert_colors – each RGB channel is complemented");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t orig = MAKE_COLOR(100, 150, 200, 255);
  t_set_pixel_direct(c, 0, 0, orig);
  t_invert_colors(c);
  uint32_t inv = canvas_get_pixel(c, 0, 0);
  ASSERT_EQUAL((int)COLOR_R(inv), 255 - 100);
  ASSERT_EQUAL((int)COLOR_G(inv), 255 - 150);
  ASSERT_EQUAL((int)COLOR_B(inv), 255 - 200);
  free(c);
  PASS();
}

void test_invert_colors_alpha_unchanged(void) {
  TEST("canvas_invert_colors – alpha channel is not modified");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  /* Write a pixel with non-opaque alpha directly */
  uint8_t *p = c->pixels;
  p[0]=50; p[1]=60; p[2]=70; p[3]=128;
  t_invert_colors(c);
  ASSERT_EQUAL((int)c->pixels[3], 128);  /* alpha must be unchanged */
  free(c);
  PASS();
}

void test_invert_colors_double_is_identity(void) {
  TEST("canvas_invert_colors – applied twice restores original");
  test_canvas_t *c = calloc(1, sizeof(test_canvas_t));
  canvas_clear(c);
  uint32_t orig = MAKE_COLOR(77, 88, 99, 255);
  t_set_pixel_direct(c, 5, 5, orig);
  t_invert_colors(c);
  t_invert_colors(c);
  ASSERT_TRUE(canvas_get_pixel(c, 5, 5) == orig);
  free(c);
  PASS();
}

// ============================================================
// canvas_resize tests
// ============================================================

void test_resize_grow_preserves_content(void) {
  TEST("canvas_resize – content in original area is preserved when growing");
  dyn_canvas_t *c = dyn_canvas_create(4, 4);
  uint32_t red = MAKE_COLOR(255, 0, 0, 255);
  dyn_set_pixel(c, 0, 0, red);
  dyn_set_pixel(c, 3, 3, red);
  ASSERT_TRUE(t_resize(c, 8, 8));
  ASSERT_EQUAL(c->w, 8);
  ASSERT_EQUAL(c->h, 8);
  ASSERT_TRUE(dyn_get_pixel(c, 0, 0) == red);
  ASSERT_TRUE(dyn_get_pixel(c, 3, 3) == red);
  dyn_canvas_free(c);
  PASS();
}

void test_resize_shrink_preserves_content(void) {
  TEST("canvas_resize – content within new bounds is preserved when shrinking");
  dyn_canvas_t *c = dyn_canvas_create(8, 8);
  uint32_t blue = MAKE_COLOR(0, 0, 255, 255);
  dyn_set_pixel(c, 1, 1, blue);
  ASSERT_TRUE(t_resize(c, 4, 4));
  ASSERT_EQUAL(c->w, 4);
  ASSERT_EQUAL(c->h, 4);
  ASSERT_TRUE(dyn_get_pixel(c, 1, 1) == blue);
  dyn_canvas_free(c);
  PASS();
}

void test_resize_grow_fills_white(void) {
  TEST("canvas_resize – new area is filled with opaque white");
  dyn_canvas_t *c = dyn_canvas_create(2, 2);
  /* Paint entire 2x2 red */
  for (int y = 0; y < 2; y++)
    for (int x = 0; x < 2; x++)
      dyn_set_pixel(c, x, y, MAKE_COLOR(255, 0, 0, 255));
  ASSERT_TRUE(t_resize(c, 4, 2));
  uint32_t white = MAKE_COLOR(255, 255, 255, 255);
  /* New columns (x=2, x=3) must be white */
  ASSERT_TRUE(dyn_get_pixel(c, 2, 0) == white);
  ASSERT_TRUE(dyn_get_pixel(c, 3, 1) == white);
  dyn_canvas_free(c);
  PASS();
}

void test_resize_same_is_noop(void) {
  TEST("canvas_resize – same dimensions is a no-op, content unchanged");
  dyn_canvas_t *c = dyn_canvas_create(10, 10);
  uint32_t red = MAKE_COLOR(200, 50, 50, 255);
  dyn_set_pixel(c, 5, 5, red);
  ASSERT_TRUE(t_resize(c, 10, 10));
  ASSERT_TRUE(dyn_get_pixel(c, 5, 5) == red);
  dyn_canvas_free(c);
  PASS();
}

void test_resize_zero_is_rejected(void) {
  TEST("canvas_resize – zero dimensions are rejected (returns false)");
  dyn_canvas_t *c = dyn_canvas_create(4, 4);
  ASSERT_FALSE(t_resize(c, 0, 4));
  ASSERT_FALSE(t_resize(c, 4, 0));
  ASSERT_EQUAL(c->w, 4);
  ASSERT_EQUAL(c->h, 4);
  dyn_canvas_free(c);
  PASS();
}

void test_resize_over_limit_is_rejected(void) {
  TEST("canvas_resize – dimensions >16384 are rejected");
  dyn_canvas_t *c = dyn_canvas_create(4, 4);
  ASSERT_FALSE(t_resize(c, 16385, 4));
  ASSERT_EQUAL(c->w, 4);
  dyn_canvas_free(c);
  PASS();
}


int main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  TEST_START("Image Editor Logic");
  test_color_eq();
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
  test_pan_clamp_vscroll_width_cost();
  test_pan_clamp_no_height_cost_for_hscroll();
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

  test_draw_rect_outline();
  test_draw_rect_outline_zero_size();
  test_draw_rect_filled();
  test_draw_ellipse_outline();
  test_draw_ellipse_filled();
  test_draw_ellipse_circle();
  test_draw_polygon_triangle();
  test_draw_polygon_single_edge();
  test_spray_deposits_pixels();
  test_spray_respects_radius();
  test_is_shape_tool();
  test_canvas_draw_line_diagonal();

  test_crop_to_selection_basic();
  test_crop_to_selection_no_selection();
  test_crop_to_selection_preserves_content();
  test_crop_to_selection_reversed_drag();

  test_flip_h_mirrors_pixels();
  test_flip_h_even_width();
  test_flip_h_single_row();
  test_flip_v_mirrors_rows();
  test_flip_v_even_height();
  test_flip_v_single_col();
  test_flip_h_double_is_identity();
  test_flip_v_double_is_identity();

  test_invert_colors_complement();
  test_invert_colors_alpha_unchanged();
  test_invert_colors_double_is_identity();

  test_resize_grow_preserves_content();
  test_resize_shrink_preserves_content();
  test_resize_grow_fills_white();
  test_resize_same_is_noop();
  test_resize_zero_is_rejected();
  test_resize_over_limit_is_rejected();

  TEST_END();
}
