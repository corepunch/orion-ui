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
#include <glob.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Fills must match and edges may rasterise differently, as text does: a pixel
// whose 3x3 neighbourhood in Chrome's image is even (no channel steps by more
// than EDGE_STEP) is inside a fill and may be off by at most FILL; one on an
// edge (Chrome flattens curves into chords) by at most EDGE.
#define EDGE_STEP 24
#define FILL      12
#define EDGE      80

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
  fixture_verdict_t verdict = matches ? FIXTURE_MATCHES : known && geometry ? FIXTURE_KNOWN : FIXTURE_DIFFERS;
  char flat[256];
  snprintf(flat, sizeof(flat), "%s", fixture);
  for (char *c = flat; *c; c++) if (*c == '/') *c = '-';
  char orion_png[300], diff_png[300];
  snprintf(orion_png, sizeof(orion_png), "build/conformance/%s.orion.png", flat);
  snprintf(diff_png, sizeof(diff_png), "build/conformance/%s.diff.png", flat);
  if (verdict == FIXTURE_MATCHES) { remove(orion_png); remove(diff_png); }
  else {
    mkdir("build", 0755); mkdir("build/conformance", 0755);
    save_image_png(orion_png, actual, w, h);
    save_image_png(diff_png, diff, w, h);
    fprintf(stderr, "[conformance] %s: %d fill pixels off by more than %d (worst %d), %d edge pixels by more than %d (worst %d)%s%s; see %s\n",
            fixture, fill_off, FILL, worst_fill, edge_off, EDGE, worst_edge, known ? ": known, " : "", known ? known : "", diff_png);
  }
  free(diff);
  image_free(reference);
  free(actual);
  return verdict;
}

static void test_fixtures(const char *suite) {
  char pattern[256], title[128];
  snprintf(pattern, sizeof(pattern), "tests/conformance/%s/*.orion", suite);
  glob_t g = {0};
  glob(pattern, 0, NULL, &g);
  for (size_t i = 0; i < g.gl_pathc; i++) {
    const char *name = strrchr(g.gl_pathv[i], '/') + 1;
    char fixture[160];
    snprintf(fixture, sizeof(fixture), "%s/%.*s", suite, (int)(strlen(name) - strlen(".orion")), name);
    snprintf(title, sizeof(title), "%s matches Chrome", fixture);
    TEST(title);
    fixture_verdict_t verdict = compare_fixture(g.gl_pathv[i], fixture);
    if (verdict == FIXTURE_DIFFERS) { FAIL("Orion and Chrome disagree"); }
    else if (verdict == FIXTURE_MATCHES && known_reason(fixture)) { FAIL("now matches Chrome: remove it from k_known"); }
    else { PASS(); }
  }
  if (g.gl_pathc == 0) { TEST(suite); FAIL("no fixtures"); }
  globfree(&g);
}

int main(void) {
  TEST_START("CSS conformance with the browser");
  if (!test_graphics_init(640, 480)) { fprintf(stderr, "[conformance] no offscreen OpenGL\n"); return 1; }
  test_env_init();
  set_theme(THEME_MODERN);
  test_fixtures("boxes");
  test_fixtures("layout");
  test_graphics_shutdown();
  TEST_END();
}
