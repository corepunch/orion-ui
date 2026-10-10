// SKIN: loads a Winamp 2.x skin (folder or .wsz zip) and composes its sprites
// into skin-resolution canvases.

#include "winamp.h"
#include <ctype.h>
#include <sys/stat.h>

static const char *kSkinFiles[SKIN_COUNT] = {
  "main.bmp", "titlebar.bmp", "cbuttons.bmp", "shufrep.bmp", "posbar.bmp", "volume.bmp", "balance.bmp",
  "playpaus.bmp", "monoster.bmp", "numbers.bmp", "text.bmp", "eqmain.bmp", "pledit.bmp",
};

// TEXT.BMP character cells: three rows of 5x6 glyphs.
static const char *kTextRows[3] = {
  "abcdefghijklmnopqrstuvwxyz\"@   ",
  "0123456789\x01.:()-'!_+\\/[]^&%,=$#",
  "\x02\x03\x04?*",
};

static uint32_t rgba(int r, int g, int b) { return 0xFF000000u | (uint32_t)b << 16 | (uint32_t)g << 8 | (uint32_t)r; }

static uint8_t *read_file(const char *path, size_t *size) {
  FILE *fp = fopen(path, "rb");
  if (!fp) return NULL;
  uint8_t *data = NULL;
  long n = fseek(fp, 0, SEEK_END) ? -1 : ftell(fp);
  if (n > 0 && !fseek(fp, 0, SEEK_SET) && (data = malloc((size_t)n + 1))) {
    if (fread(data, 1, (size_t)n, fp) != (size_t)n) { free(data); data = NULL; }
    else data[n] = 0;
  }
  fclose(fp);
  if (data) *size = (size_t)n;
  return data;
}

// Folder lookups ignore case: skins ship MAIN.BMP, Main.bmp and main.bmp alike.
static uint8_t *dir_extract(const char *dir, const char *want, size_t *size) {
  char path[1024];
  snprintf(path, sizeof(path), "%s/%s", dir, want);
  uint8_t *data = read_file(path, size);
  if (data) return data;
  char upper[64];
  int n = 0;
  for (; want[n] && n < (int)sizeof(upper) - 1; n++) upper[n] = (char)toupper((unsigned char)want[n]);
  upper[n] = 0;
  snprintf(path, sizeof(path), "%s/%s", dir, upper);
  return read_file(path, size);
}

typedef struct { const char *path; uint8_t *image; zip_t *zip; } skin_source_t;

static uint8_t *source_extract(skin_source_t *src, const char *name, size_t *size) {
  if (!src->zip) return dir_extract(src->path, name, size);
  int i = zip_find(src->zip, name);
  return i < 0 ? NULL : zip_extract(src->zip, i, size);
}

static void parse_viscolor(wa_skin_t *s, const char *text) {
  int i = 0;
  for (const char *p = text; p && *p && i < 24; i++) {
    int r, g, b;
    if (sscanf(p, " %d , %d , %d", &r, &g, &b) == 3) s->vis[i] = rgba(r, g, b);
    p = strchr(p, '\n');
    if (p) p++;
  }
}

static bool parse_hex_color(const char *text, const char *key, uint32_t *out) {
  const char *p = text;
  size_t n = strlen(key);
  while ((p = strstr(p, key))) {
    bool line_start = p == text || p[-1] == '\n' || p[-1] == '\r';
    const char *v = p + n;
    while (*v == ' ') v++;
    if (line_start && *v == '=') {
      v++;
      while (*v == ' ' || *v == '#') v++;
      unsigned c;
      if (sscanf(v, "%6x", &c) == 1) { *out = rgba(c >> 16 & 255, c >> 8 & 255, c & 255); return true; }
    }
    p += n;
  }
  return false;
}

static void skin_defaults(wa_skin_t *s) {
  static const uint8_t vis[24][3] = {
    {0,0,0},{24,33,41},{239,49,16},{206,41,16},{214,90,0},{214,102,0},{214,115,0},{198,123,8},{222,165,24},
    {214,181,33},{189,222,41},{148,222,33},{41,206,16},{50,190,16},{57,181,16},{49,156,8},{41,148,0},{24,132,8},
    {255,255,255},{214,214,222},{181,189,189},{160,170,175},{148,156,165},{150,150,150},
  };
  for (int i = 0; i < 24; i++) s->vis[i] = rgba(vis[i][0], vis[i][1], vis[i][2]);
  s->pl_normal = rgba(0, 226, 0);
  s->pl_current = rgba(255, 255, 255);
  s->pl_normal_bg = rgba(0, 0, 0);
  s->pl_selected_bg = rgba(0, 0, 198);
}

void skin_free(wa_skin_t *s) {
  for (int i = 0; i < SKIN_COUNT; i++) { bitmap_free(s->bmp[i]); s->bmp[i] = NULL; }
}

bool skin_load(wa_skin_t *s, const char *path) {
  skin_source_t src = { path, NULL, NULL };
  struct stat st;
  if (stat(path, &st)) { fprintf(stderr, "[wa] skin not found path=%s\n", path); fflush(stderr); return false; }
  if (!S_ISDIR(st.st_mode)) {
    size_t zip_size = 0;
    if (!(src.image = read_file(path, &zip_size)) || !(src.zip = zip_open_memory(src.image, zip_size))) {
      fprintf(stderr, "[wa] skin read failed path=%s\n", path);
      fflush(stderr);
      free(src.image);
      return false;
    }
  }
  wa_skin_t next = {0};
  skin_defaults(&next);
  bool ok = true;
  for (int i = 0; i < SKIN_COUNT; i++) {
    size_t size = 0;
    uint8_t *data = source_extract(&src, kSkinFiles[i], &size);
    if (!data && i == SKIN_NUMBERS) data = source_extract(&src, "nums_ex.bmp", &size);
    if (data) next.bmp[i] = bitmap_load_memory(data, size);
    free(data);
    if (!next.bmp[i]) { fprintf(stderr, "[wa] skin %s missing or unreadable in %s\n", kSkinFiles[i], path); fflush(stderr); ok = false; }
  }
  size_t size = 0;
  char *text = (char *)source_extract(&src, "viscolor.txt", &size);
  if (text) { parse_viscolor(&next, text); free(text); }
  if ((text = (char *)source_extract(&src, "pledit.txt", &size))) {
    parse_hex_color(text, "Normal", &next.pl_normal);
    parse_hex_color(text, "Current", &next.pl_current);
    parse_hex_color(text, "NormalBG", &next.pl_normal_bg);
    parse_hex_color(text, "SelectedBG", &next.pl_selected_bg);
    free(text);
  }
  zip_close(src.zip);
  free(src.image);
  // A skin missing a bitmap keeps the default skin's bitmap for that sheet.
  for (int i = 0; i < SKIN_COUNT; i++) {
    if (next.bmp[i]) { bitmap_free(s->bmp[i]); s->bmp[i] = next.bmp[i]; }
  }
  memcpy(s->vis, next.vis, sizeof(s->vis));
  s->pl_normal = next.pl_normal; s->pl_current = next.pl_current;
  s->pl_normal_bg = next.pl_normal_bg; s->pl_selected_bg = next.pl_selected_bg;
  return ok;
}

// The bundled classic skin is used as shipped, as a .wsz; the unpacked folder is the fallback.
bool skin_load_default(wa_skin_t *s) {
  static const char *kDefaults[] = { "base-2.91.wsz", "skin" };
  skin_defaults(s);
  for (int i = 0; i < (int)ARRAY_LEN(kDefaults); i++) {
    char path[1024];
    struct stat st;
    snprintf(path, sizeof(path), "%s/../share/winamp/%s", ui_get_exe_dir(), kDefaults[i]);
    if (stat(path, &st)) snprintf(path, sizeof(path), "apps/winamp/share/%s", kDefaults[i]);
    if (!stat(path, &st) && skin_load(s, path)) return true;
  }
  return false;
}

// ── Skin surface ────────────────────────────────────────────────────────────
// Views address a window in skin pixels; sprites reach the screen through
// gdi's BitBlt at g_app->pt_per_px, so no pixels are composed on the CPU.

irect16_t skin_rect(irect16_t r) {
  float pt = g_app ? g_app->pt_per_px : 1.0f;
  int x0 = (int)lroundf(r.x * pt), y0 = (int)lroundf(r.y * pt);
  return R(x0, y0, (int)lroundf((r.x + r.w) * pt) - x0, (int)lroundf((r.y + r.h) * pt) - y0);
}

window_t *skin_add_control(window_t *parent, const char *class_name, uint16_t id) {
  irect16_t r = R(0, 0, 1, 1);
  window_t *w = create_window_class("", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTABSTOP, &r, parent, class_name, g_app->hinstance, NULL);
  if (!w) { fprintf(stderr, "[wa] %s creation failed id=%u\n", class_name, id); fflush(stderr); return NULL; }
  w->id = id;
  return w;
}

void skin_place(window_t *child, irect16_t skin_px) {
  irect16_t r = skin_rect(skin_px);
  move_window(child, r.x, r.y);
  resize_window(child, r.w, r.h);
}

int skin_slider_pos(window_t *parent, uint16_t id) {
  window_t *c = get_window_item(parent, id);
  return c ? (int)send_message(c, slGetPos, 0, NULL) : 0;
}

bool skin_slider_dragging(window_t *parent, uint16_t id) {
  window_t *c = get_window_item(parent, id);
  return c && send_message(c, spsIsDragging, 0, NULL);
}

void skin_button_sprites(int sheet, irect16_t up, irect16_t down, sprite_button_t *out) {
  *out = (sprite_button_t){ .bm = g_app->skin.bmp[sheet], .up = up, .down = down };
}

void skin_toggle_sprites(int sheet, irect16_t up, irect16_t down, irect16_t on, irect16_t on_down, sprite_button_t *out) {
  *out = (sprite_button_t){ .bm = g_app->skin.bmp[sheet], .up = up, .down = down, .on = on, .on_down = on_down, .toggle = true };
}

bool canvas_resize(wa_canvas_t *c, int w, int h) {
  if (w <= 0 || h <= 0) { fprintf(stderr, "[wa] canvas resize rejected %dx%d\n", w, h); fflush(stderr); return false; }
  c->w = w; c->h = h;
  return true;
}

void canvas_free(wa_canvas_t *c) { *c = (wa_canvas_t){0}; }

void canvas_fill(wa_canvas_t *c, irect16_t r, uint32_t color) { (void)c; fill_rect(color, skin_rect(r)); }

void canvas_blit(wa_canvas_t *c, int sheet, irect16_t src, int dx, int dy) {
  (void)c;
  stretch_blt(skin_rect(R(dx, dy, src.w, src.h)), g_app->skin.bmp[sheet], src);
}

void canvas_tile(wa_canvas_t *c, int sheet, irect16_t src, irect16_t dst) {
  for (int y = dst.y; y < dst.y + dst.h; y += src.h)
    for (int x = dst.x; x < dst.x + dst.w; x += src.w)
      canvas_blit(c, sheet, R(src.x, src.y, MIN(src.w, dst.x + dst.w - x), MIN(src.h, dst.y + dst.h - y)), x, y);
}

static ipoint16_t glyph_cell(unsigned char ch) {
  if (ch >= 'A' && ch <= 'Z') ch = (unsigned char)tolower(ch);
  for (int r = 0; r < 3; r++) {
    const char *at = strchr(kTextRows[r], ch);
    if (ch && at) return (ipoint16_t){ (int16_t)((at - kTextRows[r]) * 5), (int16_t)(r * 6) };
  }
  return (ipoint16_t){ 30 * 5, 0 };   // space
}

// Draws skin-font text clipped to max_w, scrolled left by scroll_px (wrapping).
void canvas_text(wa_canvas_t *c, const char *text, int x, int y, int max_w, int scroll_px) {
  int len = (int)strlen(text);
  int total = len * 5;
  canvas_tile(c, SKIN_TEXT, R(150, 0, 5, 6), R(x, y, max_w, 6));
  if (!len) return;
  int start = total > max_w ? scroll_px % total : 0;
  for (int px = 0; px < max_w; ) {
    int at = (start + px) % (total > max_w ? total : INT16_MAX);
    int i = at / 5, off = at % 5;
    if (i >= len) break;
    ipoint16_t cell = glyph_cell((unsigned char)text[i]);
    int w = MIN(5 - off, max_w - px);
    canvas_blit(c, SKIN_TEXT, R(cell.x + off, cell.y, w, 6), x + px, y);
    px += w;
  }
}

void canvas_digit(wa_canvas_t *c, int digit, int x, int y) {
  canvas_blit(c, SKIN_NUMBERS, R(digit * 9, 0, 9, 13), x, y);
}

// Window-local pointer position (wparam) to canvas pixels.
ipoint16_t skin_point(window_t *win, const wa_canvas_t *c, uint32_t wparam) {
  int x = (int16_t)LOWORD(wparam), y = (int16_t)HIWORD(wparam);
  int w = MAX(1, win->frame.w), h = MAX(1, win->frame.h);
  return (ipoint16_t){ (int16_t)(x * c->w / w), (int16_t)(y * c->h / h) };
}
