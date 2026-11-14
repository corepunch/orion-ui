// css_conformance_test.c — the same CSS gives the same picture as the browser.
// Each fixture in tests/conformance/<suite>/ is a form (<name>.orion) and its
// style sheet (<name>.css); <name>.png is headless Chrome's rendering of the
// same page (tools/conformance/render.py). Orion paints the form offscreen and
// the two images must agree: fills closely, antialiased edges loosely.
// A failing fixture leaves build/conformance/<suite>-<name>.orion.png and
// .diff.png beside the reference for inspection.

#include "test_framework.h"
#include "test_env.h"
#include "test_graphics.h"
#include <orion/commctl/commctl.h>
#include <orion/commctl/form_loader.h>
#include <orion/user/image.h>
#include <orion/user/theme.h>
#include <dirent.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Fills must match and edges may rasterise differently, as text does: a pixel
// whose 3x3 neighbourhood in Chrome's image is even (no channel steps by more
// than EDGE_STEP) is inside a fill and may be off by at most FILL; one on an
// edge (Chrome flattens curves into chords) by at most EDGE. A soft shadow's
// blur is the widest fill difference: 12 on macOS, 13 on Mesa's software GL.
#define EDGE_STEP 24
#define FILL      14
#define EDGE      80

// Text rasterises differently in every engine (Chrome hints and positions glyphs on
// fractions of a pixel, Orion on whole ones), so the text suite compares where the ink
// is, not which pixels it covers: each line of ink (a run of rows that differ from the
// row's background by at least INK) must have the same bounding box in both images to
// within BOX pixels, and the total ink (distance from the background, in whole pixels)
// must agree to within INK_TOTAL percent (Chrome on macOS sets light text on dark up to 17% heavier
// than FreeType does: a difference of weight, not of placement).
#define INK         96
#define BOX         1
#define INK_TOTAL   20
#define MAX_LINES   64

// Known divergences, with their reason: such a fixture must still agree in
// geometry (no pixel off by more than GEOMETRY), and the test fails once it
// matches outright, so the list stays true.
#define GEOMETRY 128
static const struct { const char *fixture, *reason; } k_known[] = {
  {NULL, NULL},
};

static const char *known_reason(const char *fixture) {
  for (size_t i = 0; i < ARRAY_LEN(k_known); i++)
    if (k_known[i].fixture && !strcmp(k_known[i].fixture, fixture)) return k_known[i].reason;
  return NULL;
}

static result_t fixture_root(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)msg; (void)wparam; (void)lparam;
  return false;
}

static char *read_text(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *text = malloc((size_t)n + 1);
  if (text && fread(text, 1, (size_t)n, f) != (size_t)n) { free(text); text = NULL; }
  if (text) text[n] = 0;
  fclose(f);
  return text;
}

// Paints the fixture's form into its root surface and reads it back.
static uint8_t *paint_fixture(const char *orion, const char *css_path, int *w, int *h) {
  char *css = read_text(css_path);
  form_library_t *lib = form_library_load(orion);
  const form_def_t *def = lib ? form_library_form(lib, "fixture") : NULL;
  if (!css || !def) {
    fprintf(stderr, "[conformance] %s: cannot load the form or %s\n", orion, css_path);
    free(css); form_library_free(lib);
    return NULL;
  }
  ui_set_app_stylesheet(css, css_path);
  form_def_t form = *def;
  form.flags |= WINDOW_NOTITLE;
  window_t *root = create_window_from_form(&form, 0, 0, NULL, fixture_root, 0, NULL);
  uint8_t *pixels = NULL;
  if (root) {
    g_ui_runtime.running = true;
    send_message(root, evPaint, 0, NULL);
    g_ui_runtime.running = false;
    window_surface_t *surf = window_surface(root);
    *w = surf ? surf->w : 0; *h = surf ? surf->h : 0;
    pixels = surf ? malloc((size_t)*w * *h * 4) : NULL;
    if (pixels && !R_ReadTextureSRGBA8(surf->tex, *w, *h, pixels)) { free(pixels); pixels = NULL; }
    destroy_window(root);
  }
  ui_set_app_stylesheet(NULL, NULL);
  form_library_free(lib);
  free(css);
  return pixels;
}

typedef struct { int y0, y1, x0, x1; double ink; } ink_line_t;

// Lines of ink in an RGBA image; the background of a row is its most common colour.
static int ink_lines(const uint8_t *px, int w, int h, ink_line_t *out, double *total) {
  int n = 0;
  bool open = false;
  *total = 0;
  for (int y = 0; y <= h; y++) {
    int x0 = w, x1 = -1;
    double row_ink = 0;
    if (y < h) {
      const uint8_t *row = px + (size_t)y * w * 4;
      uint32_t bg = 0; int best = 0;
      for (int a = 0; a < w; a += 7) {   // sample candidates, count exactly
        int count = 0;
        for (int b = 0; b < w; b++) count += !memcmp(row + a * 4, row + b * 4, 4);
        if (count > best) { best = count; bg = (uint32_t)a; }
      }
      for (int x = 0; x < w; x++) {
        int d = 0;
        for (int c = 0; c < 3; c++) d = MAX(d, abs(row[x * 4 + c] - row[bg * 4 + c]));
        row_ink += d / 255.0;
        if (d >= INK) { x0 = MIN(x0, x); x1 = MAX(x1, x); }
      }
    }
    *total += row_ink;
    if (x1 >= 0 && n < MAX_LINES) {
      if (!open) { out[n++] = (ink_line_t){ y, y, x0, x1, 0 }; open = true; }
      else { out[n - 1].y1 = y; out[n - 1].x0 = MIN(out[n - 1].x0, x0); out[n - 1].x1 = MAX(out[n - 1].x1, x1); }
      out[n - 1].ink += row_ink;
    } else open = false;
  }
  return n;
}

// Compares the ink of two renderings; why says what differs. geometry is whether the lines
// are at least in the same place within a few pixels.
static bool text_matches(const uint8_t *reference, const uint8_t *actual, int w, int h, char *why, size_t n, bool *geometry) {
  ink_line_t a[MAX_LINES], b[MAX_LINES];
  double ink_a, ink_b;
  int na = ink_lines(reference, w, h, a, &ink_a), nb = ink_lines(actual, w, h, b, &ink_b);
  why[0] = 0; *geometry = na == nb;
  if (na != nb) { snprintf(why, n, "Chrome has %d lines of ink, Orion %d", na, nb); return false; }
  bool ok = true;
  for (int i = 0; i < na; i++) {
    int off = MAX(MAX(abs(a[i].y0 - b[i].y0), abs(a[i].y1 - b[i].y1)), MAX(abs(a[i].x0 - b[i].x0), abs(a[i].x1 - b[i].x1)));
    if (off > BOX * 3) *geometry = false;
    if (off > BOX && ok) {
      snprintf(why, n, "line %d: Chrome y%d-%d x%d-%d, Orion y%d-%d x%d-%d", i + 1,
               a[i].y0, a[i].y1, a[i].x0, a[i].x1, b[i].y0, b[i].y1, b[i].x0, b[i].x1);
      ok = false;
    }
  }
  if (ok && fabs(ink_b - ink_a) > ink_a * INK_TOTAL / 100.0) {
    snprintf(why, n, "total ink: Chrome %.0f, Orion %.0f", ink_a, ink_b);
    ok = false;
  }
  return ok;
}

typedef enum { FIXTURE_MATCHES, FIXTURE_KNOWN, FIXTURE_DIFFERS } fixture_verdict_t;

static fixture_verdict_t compare_fixture(const char *orion, const char *fixture) {
  char base[512], css[520], png[520];
  snprintf(base, sizeof(base), "%.*s", (int)(strlen(orion) - strlen(".orion")), orion);
  snprintf(css, sizeof(css), "%s.css", base);
  snprintf(png, sizeof(png), "%s.png", base);
  int rw = 0, rh = 0, w = 0, h = 0;
  uint8_t *reference = load_image(png, &rw, &rh);
  uint8_t *actual = paint_fixture(orion, css, &w, &h);
  bool ok = reference && actual && rw == w && rh == h;
  if (reference && actual && !ok)
    fprintf(stderr, "[conformance] %s: Orion paints %dx%d, Chrome %dx%d\n", orion, w, h, rw, rh);
  if (!ok) { image_free(reference); free(actual); return FIXTURE_DIFFERS; }

  int fill_off = 0, edge_off = 0, worst_fill = 0, worst_edge = 0;
  uint8_t *diff = calloc((size_t)w * h * 4, 1);
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    int i = y * w + x, d = 0, step = 0;
    for (int c = 0; c < 4; c++) d = MAX(d, abs(actual[i * 4 + c] - reference[i * 4 + c]));
    for (int ny = MAX(0, y - 1); ny <= MIN(h - 1, y + 1); ny++)
      for (int nx = MAX(0, x - 1); nx <= MIN(w - 1, x + 1); nx++)
        for (int c = 0; c < 4; c++) step = MAX(step, abs(reference[(ny * w + nx) * 4 + c] - reference[i * 4 + c]));
    bool edge = step > EDGE_STEP;
    if (edge) { worst_edge = MAX(worst_edge, d); edge_off += d > EDGE; }
    else { worst_fill = MAX(worst_fill, d); fill_off += d > FILL; }
    bool bad = edge ? d > EDGE : d > FILL;
    uint8_t v = (uint8_t)MIN(255, d * 4);
    diff[i * 4 + 0] = bad ? 255 : v; diff[i * 4 + 1] = bad ? 0 : v; diff[i * 4 + 2] = bad ? 0 : v; diff[i * 4 + 3] = 255;
  }
  const char *known = known_reason(fixture);
  bool matches = fill_off == 0 && edge_off == 0;
  bool geometry = MAX(worst_fill, worst_edge) <= GEOMETRY;
  char why[200] = "";
  bool text = !strncmp(fixture, "text/", 5);
  if (text) matches = text_matches(reference, actual, w, h, why, sizeof(why), &geometry);
  fixture_verdict_t verdict = matches ? FIXTURE_MATCHES : known && geometry ? FIXTURE_KNOWN : FIXTURE_DIFFERS;
  char flat[256];
  snprintf(flat, sizeof(flat), "%s", fixture);
  for (char *c = flat; *c; c++) if (*c == '/') *c = '-';
  char orion_png[300], diff_png[300];
  snprintf(orion_png, sizeof(orion_png), "build/conformance/%s.orion.png", flat);
  snprintf(diff_png, sizeof(diff_png), "build/conformance/%s.diff.png", flat);
  if (verdict == FIXTURE_MATCHES) { remove(orion_png); remove(diff_png); }
  else {
    test_mkdir("build", 0755); test_mkdir("build/conformance", 0755);
    save_image_png(orion_png, actual, w, h);
    save_image_png(diff_png, diff, w, h);
    if (text) fprintf(stderr, "[conformance] %s: %s%s%s; see %s\n", fixture, why, known ? ": known, " : "", known ? known : "", diff_png);
    else fprintf(stderr, "[conformance] %s: %d fill pixels off by more than %d (worst %d), %d edge pixels by more than %d (worst %d)%s%s; see %s\n",
            fixture, fill_off, FILL, worst_fill, edge_off, EDGE, worst_edge, known ? ": known, " : "", known ? known : "", diff_png);
  }
  free(diff);
  image_free(reference);
  free(actual);
  return verdict;
}

static int compare_paths(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void test_fixtures(const char *suite) {
  char dir[256], title[128];
  snprintf(dir, sizeof(dir), "tests/conformance/%s", suite);
  char *paths[512]; size_t npaths = 0;
  DIR *d = opendir(dir);
  for (struct dirent *e = d ? readdir(d) : NULL; e && npaths < 512; e = readdir(d)) {
    size_t n = strlen(e->d_name);
    if (n <= 6 || strcmp(e->d_name + n - 6, ".orion")) continue;
    size_t len = strlen(dir) + n + 2;
    paths[npaths] = malloc(len);
    snprintf(paths[npaths++], len, "%s/%s", dir, e->d_name);
  }
  if (d) closedir(d);
  qsort(paths, npaths, sizeof(*paths), compare_paths);
  for (size_t i = 0; i < npaths; i++) {
    const char *name = strrchr(paths[i], '/') + 1;
    char fixture[160];
    snprintf(fixture, sizeof(fixture), "%s/%.*s", suite, (int)(strlen(name) - strlen(".orion")), name);
    snprintf(title, sizeof(title), "%s matches Chrome", fixture);
    TEST(title);
    fixture_verdict_t verdict = compare_fixture(paths[i], fixture);
    if (verdict == FIXTURE_DIFFERS) { FAIL("Orion and Chrome disagree"); }
    else if (verdict == FIXTURE_MATCHES && known_reason(fixture)) { FAIL("now matches Chrome: remove it from k_known"); }
    else { PASS(); }
    free(paths[i]);
  }
  if (npaths == 0) { TEST(suite); FAIL("no fixtures"); }
}

int main(void) {
  TEST_START("CSS conformance with the browser");
  if (!test_graphics_init(640, 480)) { fprintf(stderr, "[conformance] no offscreen OpenGL\n"); return 1; }
  test_env_init();
  set_theme(THEME_MODERN);
  test_fixtures("boxes");
  test_fixtures("layout");
  test_fixtures("text");
  test_graphics_shutdown();
  TEST_END();
}
