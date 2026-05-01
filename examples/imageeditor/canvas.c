// Canvas operations: pixel drawing, PNG I/O, GL texture management

#include "imageeditor.h"

// ============================================================
// Layer management helpers
// ============================================================

// Allocate a new layer with an opaque-white pixel buffer.
static layer_t *layer_new(int w, int h, const char *name) {
  layer_t *lay = calloc(1, sizeof(layer_t));
  if (!lay) return NULL;
  lay->pixels = malloc((size_t)w * h * 4);
  if (!lay->pixels) { free(lay); return NULL; }
  memset(lay->pixels, 0xFF, (size_t)w * h * 4);
  lay->tex = 0;
  strncpy(lay->name, name, sizeof(lay->name) - 1);
  lay->visible = true;
  lay->opacity = 255;
  lay->blend_mode = LAYER_BLEND_NORMAL;
  return lay;
}

static void layer_free_one(layer_t *lay) {
  if (!lay) return;
  if (lay->tex)
    glDeleteTextures(1, &lay->tex);
  free(lay->pixels);
  free(lay);
}

// Crop or expand a single layer's pixel buffer.
// src_x / src_y is the top-left corner of the selection in old-canvas coords.
static bool layer_crop_expand(layer_t *lay, int old_w, int old_h,
                               int src_x, int src_y, int new_w, int new_h) {
  uint8_t *buf = malloc((size_t)new_w * new_h * 4);
  if (!buf) return false;
  memset(buf, 0xFF, (size_t)new_w * new_h * 4);

  int ix0 = MAX(src_x, 0);
  int iy0 = MAX(src_y, 0);
  int ix1 = MIN(src_x + new_w, old_w);
  int iy1 = MIN(src_y + new_h, old_h);
  int iw  = ix1 - ix0;
  int ih  = iy1 - iy0;

  if (iw > 0 && ih > 0) {
    int dcol = ix0 - src_x;
    int drow = iy0 - src_y;
    for (int r = 0; r < ih; r++) {
      const uint8_t *srow = lay->pixels + ((size_t)(iy0 + r) * old_w + ix0) * 4;
      uint8_t       *drow_ptr = buf + ((size_t)(drow + r) * new_w + dcol) * 4;
      memcpy(drow_ptr, srow, (size_t)iw * 4);
    }
  }
  free(lay->pixels);
  lay->pixels = buf;
  if (lay->tex) {
    glDeleteTextures(1, &lay->tex);
    lay->tex = 0;
  }
  lay->preview_active = false;
  return true;
}

// Composite all visible layers into dst (canvas_w * canvas_h * 4 RGBA).
// The result preserves alpha so the canvas can be drawn over a checkerboard.
static void canvas_composite(const canvas_doc_t *doc, uint8_t *dst) {
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  memset(dst, 0x00, n * 4);

  for (int li = 0; li < doc->layer_count; li++) {
    const layer_t *lay = doc->layers[li];
    if (!lay->visible) continue;

    for (size_t i = 0; i < n; i++) {
      const uint8_t *src = lay->pixels + i * 4;
      uint8_t       *d   = dst + i * 4;

      uint32_t sa = (src[3] * lay->opacity + 127) / 255;
      if (sa == 0) continue;

      uint32_t da = d[3];
      uint32_t inv = 255 - sa;
      uint32_t out_a = sa + (da * inv + 127) / 255;
      if (out_a == 0) continue;

      uint8_t br = src[0], bg = src[1], bb = src[2];
      switch (lay->blend_mode) {
        case LAYER_BLEND_MULTIPLY:
          br = (uint8_t)((uint32_t)src[0] * d[0] / 255);
          bg = (uint8_t)((uint32_t)src[1] * d[1] / 255);
          bb = (uint8_t)((uint32_t)src[2] * d[2] / 255);
          break;
        case LAYER_BLEND_SCREEN:
          br = (uint8_t)(255 - ((uint32_t)(255 - src[0]) * (255 - d[0]) / 255));
          bg = (uint8_t)(255 - ((uint32_t)(255 - src[1]) * (255 - d[1]) / 255));
          bb = (uint8_t)(255 - ((uint32_t)(255 - src[2]) * (255 - d[2]) / 255));
          break;
        case LAYER_BLEND_ADD:
          br = (uint8_t)MIN(255, (int)src[0] + (int)d[0]);
          bg = (uint8_t)MIN(255, (int)src[1] + (int)d[1]);
          bb = (uint8_t)MIN(255, (int)src[2] + (int)d[2]);
          break;
        case LAYER_BLEND_NORMAL:
        default:
          break;
      }

      uint64_t out_r = (uint64_t)br * sa * 255 +
                       (uint64_t)d[0] * da * inv;
      uint64_t out_g = (uint64_t)bg * sa * 255 +
                       (uint64_t)d[1] * da * inv;
      uint64_t out_b = (uint64_t)bb * sa * 255 +
                       (uint64_t)d[2] * da * inv;
      uint64_t denom = (uint64_t)out_a * 255;

      d[0] = (uint8_t)((out_r + denom / 2) / denom);
      d[1] = (uint8_t)((out_g + denom / 2) / denom);
      d[2] = (uint8_t)((out_b + denom / 2) / denom);
      d[3] = (uint8_t)out_a;
    }
  }
}

static void layer_upload_texture(canvas_doc_t *doc, layer_t *lay) {
  if (!doc || !lay || !lay->pixels) return;
  if (!lay->tex) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, doc->canvas_w, doc->canvas_h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, lay->pixels);
    lay->tex = tex;
  } else {
    glBindTexture(GL_TEXTURE_2D, lay->tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, doc->canvas_w, doc->canvas_h,
                    GL_RGBA, GL_UNSIGNED_BYTE, lay->pixels);
  }
}

static void layer_clear_preview_one(layer_t *lay) {
  if (!lay) return;
  lay->preview_active = false;
  lay->preview_effect = UI_RENDER_EFFECT_COPY;
  memset(&lay->preview_params, 0, sizeof(lay->preview_params));
}

// ============================================================
// Public layer management API
// ============================================================

bool doc_add_layer_filled(canvas_doc_t *doc, uint32_t fill_color) {
  if (!doc || doc->layer_count >= LAYER_MAX) return false;

  char name[64];
  if (doc->layer_count == 0)
    strncpy(name, "Background", sizeof(name) - 1);
  else
    snprintf(name, sizeof(name), "Layer %d", doc->layer_count + 1);

  layer_t *lay = layer_new(doc->canvas_w, doc->canvas_h, name);
  if (!lay) return false;

  // Fill with the requested color using 4-byte writes for efficiency.
  // malloc() returns sufficiently aligned memory for uint32_t access.
  size_t npx = (size_t)doc->canvas_w * doc->canvas_h;
  uint32_t *dst = (uint32_t *)lay->pixels;
  for (size_t i = 0; i < npx; i++)
    dst[i] = fill_color;

  layer_t **nl = realloc(doc->layers, sizeof(layer_t *) * (doc->layer_count + 1));
  if (!nl) { layer_free_one(lay); return false; }
  doc->layers = nl;
  doc->layers[doc->layer_count] = lay;
  doc->layer_count++;
  doc->active_layer = doc->layer_count - 1;
  doc->pixels = doc->layers[doc->active_layer]->pixels;
  if (doc->layer_count > 1) {
    doc->canvas_dirty = true;
    doc->modified = true;
  }
  return true;
}

bool doc_add_layer(canvas_doc_t *doc) {
  // Default fill: opaque white (matches the original layer_new() behaviour).
  return doc_add_layer_filled(doc, MAKE_COLOR(0xFF, 0xFF, 0xFF, 0xFF));
}

bool doc_delete_layer(canvas_doc_t *doc) {
  if (!doc || doc->layer_count <= 1) return false;
  int i = doc->active_layer;
  layer_free_one(doc->layers[i]);
  memmove(&doc->layers[i], &doc->layers[i + 1],
          sizeof(layer_t *) * (doc->layer_count - i - 1));
  doc->layer_count--;
  if (doc->active_layer >= doc->layer_count)
    doc->active_layer = doc->layer_count - 1;
  doc->pixels = doc->layers[doc->active_layer]->pixels;
  doc->editing_mask = false;
  doc->canvas_dirty = true;
  doc->modified = true;
  return true;
}

bool doc_duplicate_layer(canvas_doc_t *doc) {
  if (!doc || doc->layer_count >= LAYER_MAX) return false;
  const layer_t *src = doc->layers[doc->active_layer];
  size_t px_sz = (size_t)doc->canvas_w * doc->canvas_h * 4;

  layer_t *dup = calloc(1, sizeof(layer_t));
  if (!dup) return false;
  dup->pixels = malloc(px_sz);
  if (!dup->pixels) { free(dup); return false; }
  memcpy(dup->pixels, src->pixels, px_sz);
  dup->visible = src->visible;
  dup->opacity = src->opacity;
  dup->blend_mode = src->blend_mode;
  snprintf(dup->name, sizeof(dup->name), "%s copy", src->name);

  int ins = doc->active_layer + 1;
  layer_t **nl = realloc(doc->layers, sizeof(layer_t *) * (doc->layer_count + 1));
  if (!nl) { free(dup->pixels); free(dup); return false; }
  doc->layers = nl;
  memmove(&doc->layers[ins + 1], &doc->layers[ins],
          sizeof(layer_t *) * (doc->layer_count - ins));
  doc->layers[ins] = dup;
  doc->layer_count++;
  doc->active_layer = ins;
  doc->pixels = doc->layers[doc->active_layer]->pixels;
  doc->canvas_dirty = true;
  doc->modified = true;
  return true;
}

void doc_set_active_layer(canvas_doc_t *doc, int idx) {
  if (!doc || idx < 0 || idx >= doc->layer_count) return;
  doc->active_layer = idx;
  doc->pixels = doc->layers[idx]->pixels;
  doc->editing_mask = false;
  doc->canvas_dirty = true;
  if (doc->canvas_win)
    invalidate_window(doc->canvas_win);
  if (doc->canvas_win) {
    canvas_win_state_t *state = (canvas_win_state_t *)doc->canvas_win->userdata;
    if (state) {
      canvas_win_update_status(doc->canvas_win, state->hover.x, state->hover.y,
                               state->hover_valid);
    }
  }
}

void doc_set_mask_only_view(canvas_doc_t *doc, bool enabled) {
  if (!doc) return;
  if (doc->mask_only_view == enabled) return;
  doc->mask_only_view = enabled;
  doc->canvas_dirty = true;
  if (doc->canvas_win) {
    invalidate_window(doc->canvas_win);
    canvas_win_state_t *state = (canvas_win_state_t *)doc->canvas_win->userdata;
    if (state) {
      canvas_win_update_status(doc->canvas_win, state->hover.x, state->hover.y,
                               state->hover_valid);
    }
  }
  imageeditor_sync_main_toolbar();
}

void doc_move_layer_up(canvas_doc_t *doc) {
  if (!doc || doc->active_layer >= doc->layer_count - 1) return;
  int i = doc->active_layer;
  layer_t *tmp = doc->layers[i];
  doc->layers[i] = doc->layers[i + 1];
  doc->layers[i + 1] = tmp;
  doc->active_layer = i + 1;
  doc->canvas_dirty = true;
  doc->modified = true;
}

void doc_move_layer_down(canvas_doc_t *doc) {
  if (!doc || doc->active_layer == 0) return;
  int i = doc->active_layer;
  layer_t *tmp = doc->layers[i];
  doc->layers[i] = doc->layers[i - 1];
  doc->layers[i - 1] = tmp;
  doc->active_layer = i - 1;
  doc->canvas_dirty = true;
  doc->modified = true;
}

void doc_merge_down(canvas_doc_t *doc) {
  if (!doc || doc->active_layer == 0 || doc->layer_count < 2) return;
  int top_idx = doc->active_layer;
  int bot_idx = top_idx - 1;
  const layer_t *top = doc->layers[top_idx];
  layer_t       *bot = doc->layers[bot_idx];
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;

  for (size_t i = 0; i < n; i++) {
    const uint8_t *s = top->pixels + i * 4;
    uint8_t       *d = bot->pixels + i * 4;
    uint32_t sa = s[3];
    sa = (sa * top->opacity + 127) / 255;
    if (sa == 0) continue;
    uint32_t inv = 255 - sa;
    d[0] = (uint8_t)((s[0]*sa + d[0]*inv + 127)/255);
    d[1] = (uint8_t)((s[1]*sa + d[1]*inv + 127)/255);
    d[2] = (uint8_t)((s[2]*sa + d[2]*inv + 127)/255);
    d[3] = 255;
  }

  layer_free_one(doc->layers[top_idx]);
  memmove(&doc->layers[top_idx], &doc->layers[top_idx + 1],
          sizeof(layer_t *) * (doc->layer_count - top_idx - 1));
  doc->layer_count--;
  doc->active_layer = bot_idx;
  doc->pixels = doc->layers[doc->active_layer]->pixels;
  doc->canvas_dirty = true;
  doc->modified = true;
}

void doc_flatten(canvas_doc_t *doc) {
  if (!doc || doc->layer_count < 1) return;
  if (doc->layer_count == 1) {
    // Nothing to flatten.
    return;
  }

  size_t sz = (size_t)doc->canvas_w * doc->canvas_h * 4;
  uint8_t *flat = malloc(sz);
  if (!flat) return;
  canvas_composite(doc, flat);

  // Allocate the result layer BEFORE tearing down the old stack so that
  // a subsequent OOM does not leave the document in an invalid state.
  layer_t **nl = malloc(sizeof(layer_t *));
  if (!nl) { free(flat); return; }

  layer_t *bg = calloc(1, sizeof(layer_t));
  if (!bg) { free(flat); free(nl); return; }
  bg->pixels  = flat;
  bg->visible = true;
  bg->opacity = 255;
  strncpy(bg->name, "Background", sizeof(bg->name) - 1);

  // All allocations succeeded — now free the old stack.
  for (int i = 0; i < doc->layer_count; i++)
    layer_free_one(doc->layers[i]);
  free(doc->layers);

  doc->layers       = nl;
  doc->layers[0]    = bg;
  doc->layer_count  = 1;
  doc->active_layer = 0;
  doc->pixels       = bg->pixels;
  doc->editing_mask = false;
  doc->canvas_dirty = true;
  doc->modified     = true;
}

void doc_free_layers(canvas_doc_t *doc) {
  if (!doc) return;
  for (int i = 0; i < doc->layer_count; i++)
    layer_free_one(doc->layers[i]);
  free(doc->layers);
  doc->layers       = NULL;
  doc->layer_count  = 0;
  doc->active_layer = 0;
  doc->pixels       = NULL;
}

// ============================================================
// Alpha editing / mask operations
// ============================================================

bool layer_add_mask_ex(canvas_doc_t *doc, int idx, int fill_mode);

bool layer_add_mask(canvas_doc_t *doc, int idx) {
  return layer_add_mask_ex(doc, idx, MASK_EXTRACT_WHITE);
}

static uint8_t color_to_gray(uint32_t c) {
  return (uint8_t)((COLOR_R(c) * 77 + COLOR_G(c) * 150 + COLOR_B(c) * 29) >> 8);
}

static void fill_alpha_from_layer_gray(canvas_doc_t *doc, layer_t *lay) {
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  for (size_t i = 0; i < n; i++) {
    const uint8_t *px = lay->pixels + i * 4;
    lay->pixels[i * 4 + 3] = (uint8_t)((px[0] * 77 + px[1] * 150 + px[2] * 29) >> 8);
  }
}

static void fill_alpha_gray_value(uint8_t *pixels, size_t n, uint8_t v) {
  for (size_t i = 0; i < n; i++)
    pixels[i * 4 + 3] = v;
}

static uint8_t fill_mode_to_alpha(int fill_mode) {
  switch (fill_mode) {
    case MASK_EXTRACT_BACKGROUND:
      return g_app ? color_to_gray(g_app->bg_color) : 0xFF;
    case MASK_EXTRACT_FOREGROUND:
      return g_app ? color_to_gray(g_app->fg_color) : 0x00;
    case MASK_EXTRACT_WHITE:
      return 0xFF;
    case MASK_EXTRACT_GRAYSCALE:
    default:
      return 0x00; // handled separately
  }
}

const char *layer_blend_mode_name(layer_blend_mode_t mode) {
  switch (mode) {
    case LAYER_BLEND_MULTIPLY: return "Multiply";
    case LAYER_BLEND_SCREEN:    return "Screen";
    case LAYER_BLEND_ADD:       return "Add";
    case LAYER_BLEND_NORMAL:
    default:                    return "Normal";
  }
}

void doc_set_layer_blend_mode(canvas_doc_t *doc, int idx, layer_blend_mode_t mode) {
  if (!doc || idx < 0 || idx >= doc->layer_count) return;
  layer_t *lay = doc->layers[idx];
  if (!lay) return;
  lay->blend_mode = (uint8_t)CLAMP((int)mode, 0, (int)LAYER_BLEND_COUNT - 1);
  doc->canvas_dirty = true;
  doc->modified = true;
  if (doc->canvas_win)
    invalidate_window(doc->canvas_win);
}

void layer_clear_preview_effect(canvas_doc_t *doc, int idx) {
  if (!doc || idx < 0 || idx >= doc->layer_count) return;
  layer_clear_preview_one(doc->layers[idx]);
  if (doc->canvas_win)
    invalidate_window(doc->canvas_win);
}

bool layer_set_preview_effect(canvas_doc_t *doc, int idx,
                              ui_render_effect_t effect,
                              const ui_render_effect_params_t *params) {
  if (!doc || idx < 0 || idx >= doc->layer_count) return false;
  layer_t *lay = doc->layers[idx];
  if (!lay) return false;
  lay->preview_effect = effect;
  if (params)
    lay->preview_params = *params;
  else
    memset(&lay->preview_params, 0, sizeof(lay->preview_params));
  lay->preview_active = true;
  if (doc->canvas_win)
    invalidate_window(doc->canvas_win);
  return true;
}

bool layer_commit_preview_effect(canvas_doc_t *doc, int idx) {
  if (!doc || idx < 0 || idx >= doc->layer_count) return false;
  layer_t *lay = doc->layers[idx];
  if (!lay) return false;
  if (!lay->preview_active) return true;

  size_t sz = (size_t)doc->canvas_w * doc->canvas_h * 4;
  uint8_t *buf = malloc(sz);
  if (!buf) return false;
  uint32_t baked_tex = 0;
  if (!bake_texture_effect((int)lay->tex, doc->canvas_w, doc->canvas_h,
                           lay->preview_effect, &lay->preview_params, &baked_tex)) {
    free(buf);
    return false;
  }
  if (!read_texture_rgba((int)baked_tex, doc->canvas_w, doc->canvas_h, buf)) {
    R_DeleteTexture(baked_tex);
    free(buf);
    return false;
  }
  R_DeleteTexture(baked_tex);
  memcpy(lay->pixels, buf, sz);
  free(buf);
  layer_clear_preview_one(lay);
  doc->canvas_dirty = true;
  doc->modified = true;
  doc_update_title(doc);
  if (doc->canvas_win)
    invalidate_window(doc->canvas_win);
  return true;
}

bool layer_add_mask_ex(canvas_doc_t *doc, int idx, int fill_mode) {
  if (!doc || idx < 0 || idx >= doc->layer_count) return false;
  layer_t *lay = doc->layers[idx];
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  if (fill_mode == MASK_EXTRACT_GRAYSCALE) {
    fill_alpha_from_layer_gray(doc, lay);
  } else {
    fill_alpha_gray_value(lay->pixels, n, fill_mode_to_alpha(fill_mode));
  }
  doc->editing_mask = true;
  doc->canvas_dirty = true;
  doc->modified     = true;
  return true;
}

void layer_apply_mask(canvas_doc_t *doc, int idx) {
  if (!doc || idx < 0 || idx >= doc->layer_count) return;
  doc->canvas_dirty = true;
  doc->modified     = true;
  doc->editing_mask = false;
}

void layer_remove_mask(canvas_doc_t *doc, int idx) {
  if (!doc || idx < 0 || idx >= doc->layer_count) return;
  layer_t *lay = doc->layers[idx];
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  for (size_t i = 0; i < n; i++)
    lay->pixels[i * 4 + 3] = 255;
  doc->canvas_dirty = true;
  doc->modified     = true;
  doc->editing_mask = false;
}

// Open the active layer's alpha channel as a new document.
canvas_doc_t *canvas_extract_mask(canvas_doc_t *doc) {
  if (!doc || !g_app || doc->layer_count == 0) return NULL;
  layer_t *lay = doc->layers[doc->active_layer];
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;

  canvas_doc_t *new_doc = create_document(NULL, doc->canvas_w, doc->canvas_h);
  if (!new_doc) return NULL;

  uint8_t *dst = new_doc->pixels;
  for (size_t i = 0; i < n; i++) {
    uint8_t v = lay->pixels[i * 4 + 3];
    dst[i * 4 + 0] = v;
    dst[i * 4 + 1] = v;
    dst[i * 4 + 2] = v;
    dst[i * 4 + 3] = 255;
  }
  new_doc->canvas_dirty = true;
  new_doc->modified     = false;
  doc_update_title(new_doc);
  invalidate_window(new_doc->canvas_win);
  return new_doc;
}

// ============================================================
// Canvas pixel operations
// ============================================================

// Write a pixel directly (bypasses selection mask – used for paste/move commit).
static void canvas_set_pixel_direct(canvas_doc_t *doc, int x, int y, uint32_t c) {
  if (!canvas_in_bounds(doc, x, y)) return;
  uint8_t *p = doc->pixels + ((size_t)y * doc->canvas_w + x) * 4;
  p[0]=COLOR_R(c); p[1]=COLOR_G(c); p[2]=COLOR_B(c); p[3]=COLOR_A(c);
  doc->canvas_dirty = true;
  doc->modified     = true;
}

void canvas_set_pixel(canvas_doc_t *doc, int x, int y, uint32_t c) {
  if (!canvas_in_bounds(doc, x, y)) return;
  if (!canvas_in_selection(doc, x, y)) return;

  uint8_t *p = doc->pixels + ((size_t)y * doc->canvas_w + x) * 4;
  if (doc->editing_mask) {
    // Mask edits affect only alpha, while the RGB content stays intact.
    uint8_t gray = (uint8_t)((COLOR_R(c) * 77 + COLOR_G(c) * 150 + COLOR_B(c) * 29) >> 8);
    p[3] = gray;
  } else {
    p[0]=COLOR_R(c); p[1]=COLOR_G(c); p[2]=COLOR_B(c); p[3]=COLOR_A(c);
  }
  doc->canvas_dirty = true;
  doc->modified     = true;
}

uint32_t canvas_get_pixel(const canvas_doc_t *doc, int x, int y) {
  if (!canvas_in_bounds(doc, x, y)) return MAKE_COLOR(0,0,0,0);
  const uint8_t *p = doc->pixels + ((size_t)y * doc->canvas_w + x) * 4;
  return MAKE_COLOR(p[0],p[1],p[2],p[3]);
}

void canvas_clear(canvas_doc_t *doc) {
  memset(doc->pixels, 0xFF, (size_t)doc->canvas_w * doc->canvas_h * 4);
  doc->canvas_dirty = true;
  doc->modified     = false;
}

void canvas_draw_circle(canvas_doc_t *doc, int cx, int cy, int r, uint32_t c) {
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++)
      if (dx*dx + dy*dy <= r*r)
        canvas_set_pixel(doc, cx+dx, cy+dy, c);
}

void canvas_draw_line(canvas_doc_t *doc, int x0, int y0, int x1, int y1,
                      int radius, uint32_t c) {
  int dx = abs(x1-x0), dy = abs(y1-y0);
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  while (true) {
    canvas_draw_circle(doc, x0, y0, radius, c);
    if (x0==x1 && y0==y1) break;
    int e2 = 2*err;
    if (e2 > -dy) { err -= dy; x0 += sx; }
    if (e2 <  dx) { err += dx; y0 += sy; }
  }
}

void canvas_flood_fill(canvas_doc_t *doc, int sx, int sy, uint32_t fill) {
  if (!canvas_in_selection(doc, sx, sy)) return;
  uint32_t target = canvas_get_pixel(doc, sx, sy);
  if (target == fill) return;

  typedef struct { int x, y; } pt_t;
  // Use size_t arithmetic to avoid overflow for large canvases.
  size_t capacity = (size_t)doc->canvas_w * (size_t)doc->canvas_h;
  // Sanity-cap the queue at 64 M entries (~512 MB) to avoid OOM on huge images.
  if (capacity > 64 * 1024 * 1024) capacity = 64 * 1024 * 1024;
  pt_t *queue = malloc(sizeof(pt_t) * capacity);
  if (!queue) return;

  size_t head = 0, tail = 0;
  queue[tail++] = (pt_t){sx, sy};
  canvas_set_pixel(doc, sx, sy, fill);

  while (head < tail) {
    pt_t cur = queue[head++];
    int nx[4] = {cur.x+1, cur.x-1, cur.x,   cur.x};
    int ny[4] = {cur.y,   cur.y,   cur.y+1, cur.y-1};
    for (int i = 0; i < 4; i++) {
      if (canvas_in_bounds(doc, nx[i], ny[i]) &&
          canvas_in_selection(doc, nx[i], ny[i]) &&
          canvas_get_pixel(doc, nx[i], ny[i]) == target &&
          tail < capacity) {
        canvas_set_pixel(doc, nx[i], ny[i], fill);
        queue[tail++] = (pt_t){nx[i], ny[i]};
      }
    }
  }
  free(queue);
}

// Airbrush/spray: scatter random pixels within radius around (cx, cy).
// Approximately 20 dots per call using Cartesian rejection sampling
// (naturally higher density toward the center, mimicking a real airbrush).
void canvas_spray(canvas_doc_t *doc, int cx, int cy, int radius, uint32_t c) {
  int r2 = radius * radius;
  for (int i = 0; i < 20; i++) {
    int dx = (rand() % (2 * radius + 1)) - radius;
    int dy = (rand() % (2 * radius + 1)) - radius;
    if (dx * dx + dy * dy <= r2)
      canvas_set_pixel(doc, cx + dx, cy + dy, c);
  }
}

// ============================================================
// Shape drawing functions
// ============================================================

void canvas_draw_rect_outline(canvas_doc_t *doc, int x, int y, int w, int h, uint32_t c) {
  if (w <= 0 || h <= 0) return;
  canvas_draw_line(doc, x,     y,     x+w-1, y,     0, c);
  canvas_draw_line(doc, x,     y+h-1, x+w-1, y+h-1, 0, c);
  canvas_draw_line(doc, x,     y,     x,     y+h-1, 0, c);
  canvas_draw_line(doc, x+w-1, y,     x+w-1, y+h-1, 0, c);
}

void canvas_draw_rect_filled(canvas_doc_t *doc, int x, int y, int w, int h, uint32_t outline, uint32_t fill) {
  if (w <= 0 || h <= 0) return;
  for (int dy = 1; dy < h - 1; dy++)
    canvas_draw_line(doc, x+1, y+dy, x+w-2, y+dy, 0, fill);
  canvas_draw_rect_outline(doc, x, y, w, h, outline);
}

// Midpoint ellipse algorithm (Bresenham's)
void canvas_draw_ellipse_outline(canvas_doc_t *doc, int cx, int cy, int rx, int ry, uint32_t c) {
  if (rx <= 0 || ry <= 0) return;
  long rx2 = (long)rx * rx, ry2 = (long)ry * ry;
  long x = 0, y = ry;
  long dx = 2 * ry2 * x, dy = 2 * rx2 * y;
  long p = (long)(ry2 - rx2 * ry + 0.25f * rx2);

  while (dx < dy) {
    canvas_set_pixel(doc, (int)(cx+x), (int)(cy+y), c);
    canvas_set_pixel(doc, (int)(cx-x), (int)(cy+y), c);
    canvas_set_pixel(doc, (int)(cx+x), (int)(cy-y), c);
    canvas_set_pixel(doc, (int)(cx-x), (int)(cy-y), c);
    x++;
    dx += 2 * ry2;
    if (p < 0) {
      p += ry2 + dx;
    } else {
      y--; dy -= 2 * rx2; p += ry2 + dx - dy;
    }
  }
  p = (long)(ry2 * (x + 0.5f) * (x + 0.5f) + rx2 * (y-1) * (y-1) - rx2 * ry2);
  while (y >= 0) {
    canvas_set_pixel(doc, (int)(cx+x), (int)(cy+y), c);
    canvas_set_pixel(doc, (int)(cx-x), (int)(cy+y), c);
    canvas_set_pixel(doc, (int)(cx+x), (int)(cy-y), c);
    canvas_set_pixel(doc, (int)(cx-x), (int)(cy-y), c);
    y--;
    dy -= 2 * rx2;
    if (p > 0) {
      p += rx2 - dy;
    } else {
      x++; dx += 2 * ry2; p += rx2 - dy + dx;
    }
  }
}

void canvas_draw_ellipse_filled(canvas_doc_t *doc, int cx, int cy, int rx, int ry, uint32_t outline, uint32_t fill) {
  if (rx <= 0 || ry <= 0) return;
  double rx2 = (double)rx * (double)rx;
  double ry2 = (double)ry * (double)ry;
  for (int py = cy - ry; py <= cy + ry; py++) {
    if (!canvas_in_bounds(doc, cx, py)) continue;
    double dy = (double)(py - cy);
    double t = 1.0 - (dy * dy) / ry2;
    if (t <= 0.0) continue;
    int dx = (int)(sqrt(rx2 * t) + 0.5);
    canvas_draw_line(doc, cx - dx + 1, py, cx + dx - 1, py, 0, fill);
  }
  canvas_draw_ellipse_outline(doc, cx, cy, rx, ry, outline);
}

// Rounded rectangle using arc + straight edges
void canvas_draw_rounded_rect_outline(canvas_doc_t *doc, int x, int y, int w, int h, int r, uint32_t c) {
  if (w <= 0 || h <= 0) return;
  if (r < 0) r = 0;
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  // Straight edges
  canvas_draw_line(doc, x+r,   y,     x+w-r-1, y,     0, c);
  canvas_draw_line(doc, x+r,   y+h-1, x+w-r-1, y+h-1, 0, c);
  canvas_draw_line(doc, x,     y+r,   x,        y+h-r-1, 0, c);
  canvas_draw_line(doc, x+w-1, y+r,   x+w-1,   y+h-r-1, 0, c);
  // Four quarter arcs using midpoint circle algorithm
  int px = 0, py = r, d = 3 - 2*r;
  while (px <= py) {
    canvas_set_pixel(doc, x+r-px,   y+r-py,   c);
    canvas_set_pixel(doc, x+w-r+px-1, y+r-py, c);
    canvas_set_pixel(doc, x+r-py,   y+r-px,   c);
    canvas_set_pixel(doc, x+w-r+py-1, y+r-px, c);
    canvas_set_pixel(doc, x+r-px,   y+h-r+py-1, c);
    canvas_set_pixel(doc, x+w-r+px-1, y+h-r+py-1, c);
    canvas_set_pixel(doc, x+r-py,   y+h-r+px-1, c);
    canvas_set_pixel(doc, x+w-r+py-1, y+h-r+px-1, c);
    if (d < 0) { d += 4*px + 6; }
    else       { d += 4*(px-py) + 10; py--; }
    px++;
  }
}

void canvas_draw_rounded_rect_filled(canvas_doc_t *doc, int x, int y, int w, int h, int r, uint32_t outline, uint32_t fill) {
  if (w <= 0 || h <= 0) return;
  if (r < 0) r = 0;
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  // Fill rows from y+r to y+h-r (full width interior)
  for (int dy = r; dy < h - r; dy++)
    canvas_draw_line(doc, x+1, y+dy, x+w-2, y+dy, 0, fill);
  // Fill corner arcs using circle scan-line fill
  int px = 0, py = r, d = 3 - 2*r;
  while (px <= py) {
    // Fill horizontal spans for corner arcs
    canvas_draw_line(doc, x+r-py+1, y+r-px, x+w-r+py-2, y+r-px, 0, fill);
    canvas_draw_line(doc, x+r-px+1, y+r-py, x+w-r+px-2, y+r-py, 0, fill);
    canvas_draw_line(doc, x+r-py+1, y+h-r+px-1, x+w-r+py-2, y+h-r+px-1, 0, fill);
    canvas_draw_line(doc, x+r-px+1, y+h-r+py-1, x+w-r+px-2, y+h-r+py-1, 0, fill);
    if (d < 0) { d += 4*px + 6; }
    else       { d += 4*(px-py) + 10; py--; }
    px++;
  }
  canvas_draw_rounded_rect_outline(doc, x, y, w, h, r, outline);
}

// Polygon: draw edges between consecutive vertices and close the last to first
void canvas_draw_polygon_outline(canvas_doc_t *doc, const ipoint16_t *pts, int count, uint32_t c) {
  if (count < 2) return;
  for (int i = 0; i < count - 1; i++)
    canvas_draw_line(doc, pts[i].x, pts[i].y, pts[i+1].x, pts[i+1].y, 0, c);
  canvas_draw_line(doc, pts[count-1].x, pts[count-1].y, pts[0].x, pts[0].y, 0, c);
}

// Scanline fill for a closed polygon using the ray-casting / edge table approach
void canvas_draw_polygon_filled(canvas_doc_t *doc, const ipoint16_t *pts, int count, uint32_t outline, uint32_t fill) {
  if (count < 3) { canvas_draw_polygon_outline(doc, pts, count, outline); return; }
  // Find bounding box
  int y_min = pts[0].y, y_max = pts[0].y;
  for (int i = 1; i < count; i++) {
    if (pts[i].y < y_min) y_min = pts[i].y;
    if (pts[i].y > y_max) y_max = pts[i].y;
  }
  y_min = MAX(y_min, 0); y_max = MIN(y_max, doc->canvas_h - 1);
  int *xs = malloc(sizeof(int) * count * 2);
  if (!xs) { canvas_draw_polygon_outline(doc, pts, count, outline); return; }
  for (int y = y_min; y <= y_max; y++) {
    int n = 0;
    for (int i = 0, j = count - 1; i < count; j = i++) {
      int yi = pts[i].y, yj = pts[j].y;
      if ((yi <= y && yj > y) || (yj <= y && yi > y)) {
        xs[n++] = pts[i].x + (y - yi) * (pts[j].x - pts[i].x) / (yj - yi);
      }
    }
    // Sort intersections
    for (int a = 0; a < n - 1; a++)
      for (int b = a + 1; b < n; b++)
        if (xs[a] > xs[b]) { int t = xs[a]; xs[a] = xs[b]; xs[b] = t; }
    for (int a = 0; a + 1 < n; a += 2)
      canvas_draw_line(doc, xs[a], y, xs[a+1], y, 0, fill);
  }
  free(xs);
  canvas_draw_polygon_outline(doc, pts, count, outline);
}

// Returns true if tool is a shape tool that uses rubber-band dragging
bool canvas_is_shape_tool(int tool_id) {
  switch (tool_id) {
    case ID_TOOL_LINE:
    case ID_TOOL_RECT:
    case ID_TOOL_ELLIPSE:
    case ID_TOOL_ROUNDED_RECT:
      return true;
    default:
      return false;
  }
}

// Save pixel snapshot before starting a shape drag (no undo push yet)
void canvas_shape_begin(canvas_doc_t *doc, int cx, int cy) {
  size_t sz = (size_t)doc->canvas_w * doc->canvas_h * 4;
  if (!doc->shape_snapshot) {
    doc->shape_snapshot = malloc(sz);
  }
  if (doc->shape_snapshot) {
    memcpy(doc->shape_snapshot, doc->pixels, sz);
  }
  doc->shape_start.x = cx;
  doc->shape_start.y = cy;
}

// Restore snapshot and draw a preview of the current shape without pushing undo.
// shift_held constrains the shape (45° line, square, circle).
void canvas_shape_preview(canvas_doc_t *doc, int x0, int y0, int x1, int y1,
                          int tool, bool filled, uint32_t fg, uint32_t bg, bool shift_held) {
  // Restore snapshot
  if (doc->shape_snapshot) {
    memcpy(doc->pixels, doc->shape_snapshot, (size_t)doc->canvas_w * doc->canvas_h * 4);
    doc->canvas_dirty = true;
  }
  if (shift_held) {
    int dx = x1 - x0, dy = y1 - y0;
    switch (tool) {
      case ID_TOOL_LINE: {
        // Snap to nearest 45° increment
        if (abs(dx) > abs(dy) * 2) { dy = 0; }
        else if (abs(dy) > abs(dx) * 2) { dx = 0; }
        else { int s = MAX(abs(dx), abs(dy)); dx = (dx<0?-s:s); dy = (dy<0?-s:s); }
        x1 = x0 + dx; y1 = y0 + dy;
        break;
      }
      case ID_TOOL_RECT:
      case ID_TOOL_ROUNDED_RECT: {
        // Make square: use shorter dimension
        int s = MIN(abs(dx), abs(dy));
        x1 = x0 + (dx < 0 ? -s : s);
        y1 = y0 + (dy < 0 ? -s : s);
        break;
      }
      case ID_TOOL_ELLIPSE: {
        // Make circle
        int s = MIN(abs(dx), abs(dy));
        x1 = x0 + (dx < 0 ? -s : s);
        y1 = y0 + (dy < 0 ? -s : s);
        break;
      }
    }
  }
  int lx = MIN(x0, x1), rx = MAX(x0, x1);
  int ty = MIN(y0, y1), by = MAX(y0, y1);
  int w = rx - lx + 1, h = by - ty + 1;
  int cx2 = (lx + rx) / 2, cy2 = (ty + by) / 2;
  int rxa = (rx - lx + 1) / 2, rya = (by - ty + 1) / 2;
  int corner_r = MIN(8, MIN(w / 4, h / 4));

  switch (tool) {
    case ID_TOOL_LINE:
      canvas_draw_line(doc, x0, y0, x1, y1, 0, fg);
      break;
    case ID_TOOL_RECT:
      if (filled) canvas_draw_rect_filled(doc, lx, ty, w, h, fg, bg);
      else        canvas_draw_rect_outline(doc, lx, ty, w, h, fg);
      break;
    case ID_TOOL_ELLIPSE:
      if (filled) canvas_draw_ellipse_filled(doc, cx2, cy2, rxa, rya, fg, bg);
      else        canvas_draw_ellipse_outline(doc, cx2, cy2, rxa, rya, fg);
      break;
    case ID_TOOL_ROUNDED_RECT:
      if (filled) canvas_draw_rounded_rect_filled(doc, lx, ty, w, h, corner_r, fg, bg);
      else        canvas_draw_rounded_rect_outline(doc, lx, ty, w, h, corner_r, fg);
      break;
  }
}

// No-op: snapshot is kept until next shape begins or doc is freed
void canvas_shape_commit(canvas_doc_t *doc) {
  (void)doc;
}

// ============================================================
// Selection operations
// ============================================================

// Returns normalised selection bounds clamped to the canvas.
// Returns false when the clamped region is empty (no-op for callers).
static bool selection_bounds(const canvas_doc_t *doc,
                             int *x0, int *y0, int *x1, int *y1) {
  *x0 = MIN(doc->sel_start.x, doc->sel_end.x);
  *y0 = MIN(doc->sel_start.y, doc->sel_end.y);
  *x1 = MAX(doc->sel_start.x, doc->sel_end.x);
  *y1 = MAX(doc->sel_start.y, doc->sel_end.y);
  // Clamp to canvas bounds so callers are safe against out-of-range coords.
  if (*x0 < 0) *x0 = 0;
  if (*y0 < 0) *y0 = 0;
  if (*x1 >= doc->canvas_w) *x1 = doc->canvas_w - 1;
  if (*y1 >= doc->canvas_h) *y1 = doc->canvas_h - 1;
  return (*x0 <= *x1 && *y0 <= *y1);
}

// Copy the selected region into the app clipboard.
void canvas_copy_selection(canvas_doc_t *doc) {
  if (!doc || !doc->sel_active || !g_app) return;
  int x0, y0, x1, y1;
  if (!selection_bounds(doc, &x0, &y0, &x1, &y1)) return;
  int w = x1 - x0 + 1;
  int h = y1 - y0 + 1;
  uint8_t *buf = malloc((size_t)w * h * 4);
  if (!buf) return;
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      uint32_t c = canvas_get_pixel(doc, x0 + col, y0 + row);
      uint8_t *p = buf + ((size_t)row * w + col) * 4;
      p[0]=COLOR_R(c); p[1]=COLOR_G(c); p[2]=COLOR_B(c); p[3]=COLOR_A(c);
    }
  }
  free(g_app->clipboard);
  g_app->clipboard   = buf;
  g_app->clipboard_w = w;
  g_app->clipboard_h = h;
}

// Fill the selected region with fill_color.
void canvas_clear_selection(canvas_doc_t *doc, uint32_t fill) {
  if (!doc || !doc->sel_active) return;
  int x0, y0, x1, y1;
  if (!selection_bounds(doc, &x0, &y0, &x1, &y1)) return;
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
      canvas_set_pixel(doc, x, y, fill);
}

// Copy selection to clipboard, then clear the selection region.
void canvas_cut_selection(canvas_doc_t *doc, uint32_t fill) {
  if (!doc) return;
  canvas_copy_selection(doc);
  canvas_clear_selection(doc, fill);
}

// Paste clipboard pixels at (0, 0), bypassing the selection mask.
// The pasted region becomes the new selection.
void canvas_paste_clipboard(canvas_doc_t *doc) {
  if (!doc || !g_app || !g_app->clipboard) return;
  doc_push_undo(doc);
  int w = g_app->clipboard_w;
  int h = g_app->clipboard_h;
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      const uint8_t *p = g_app->clipboard + ((size_t)row * w + col) * 4;
      canvas_set_pixel_direct(doc, col, row, MAKE_COLOR(p[0], p[1], p[2], p[3]));
    }
  }
  // Select the pasted region (clamped to canvas bounds)
  doc->sel_active   = true;
  doc->sel_start    = (ipoint16_t){0, 0};
  int sel_x1 = w - 1;
  int sel_y1 = h - 1;
  if (sel_x1 >= doc->canvas_w) sel_x1 = doc->canvas_w - 1;
  if (sel_y1 >= doc->canvas_h) sel_y1 = doc->canvas_h - 1;
  doc->sel_end      = (ipoint16_t){sel_x1, sel_y1};
}

// Select the entire canvas.
void canvas_select_all(canvas_doc_t *doc) {
  if (!doc) return;
  doc->sel_active   = true;
  doc->sel_start    = (ipoint16_t){0, 0};
  doc->sel_end      = (ipoint16_t){doc->canvas_w - 1, doc->canvas_h - 1};
}

// Clear selection (no-op on pixels).
void canvas_deselect(canvas_doc_t *doc) {
  if (!doc) return;
  // Commit any in-progress move before deselecting.
  if (doc->sel_moving) canvas_commit_move(doc);
  doc->sel_active = false;
}

// Crop the canvas to the active selection: copy the selected pixels, fill the
// entire canvas with white, then stamp the copied pixels at the top-left
// corner (0,0).  The selection is cleared afterwards.
void canvas_crop_to_selection(canvas_doc_t *doc) {
  if (!doc || !doc->sel_active) return;
  int x0, y0, x1, y1;
  if (!selection_bounds(doc, &x0, &y0, &x1, &y1)) return;
  int w = x1 - x0 + 1;
  int h = y1 - y0 + 1;
  uint8_t *buf = malloc((size_t)w * h * 4);
  if (!buf) return;
  // Copy the selected region into the temporary buffer.
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      uint32_t c = canvas_get_pixel(doc, x0 + col, y0 + row);
      uint8_t *p = buf + ((size_t)row * w + col) * 4;
      p[0] = COLOR_R(c); p[1] = COLOR_G(c); p[2] = COLOR_B(c); p[3] = COLOR_A(c);
    }
  }
  // Fill the entire canvas with white.
  canvas_clear(doc);
  // Stamp the copied region at (0,0), clipping to the canvas dimensions.
  for (int row = 0; row < h && row < doc->canvas_h; row++) {
    for (int col = 0; col < w && col < doc->canvas_w; col++) {
      const uint8_t *p = buf + ((size_t)row * w + col) * 4;
      uint32_t c = MAKE_COLOR(p[0], p[1], p[2], p[3]);
      canvas_set_pixel_direct(doc, col, row, c);
    }
  }
  free(buf);
  doc->sel_active = false;
  doc->canvas_dirty = true;
}

// Crop or expand the canvas to the active selection rectangle.
// Unlike canvas_crop_to_selection(), the selection may extend outside the
// current canvas bounds — in that case the canvas grows to fit, with the new
// areas filled with opaque white.  If the selection is entirely inside the
// canvas the canvas shrinks (crop).  The existing pixels within the
// intersection of old and new bounds are preserved in place.
// Returns true on success, false if the state is invalid, the requested size
// exceeds the maximum, or memory allocation fails (canvas is unchanged).
bool canvas_crop_or_expand_to_selection(canvas_doc_t *doc) {
  if (!doc || !doc->sel_active) return false;
  int x0 = MIN(doc->sel_start.x, doc->sel_end.x);
  int y0 = MIN(doc->sel_start.y, doc->sel_end.y);
  int x1 = MAX(doc->sel_start.x, doc->sel_end.x);
  int y1 = MAX(doc->sel_start.y, doc->sel_end.y);
  int new_w = x1 - x0 + 1;
  int new_h = y1 - y0 + 1;

  if (new_w <= 0 || new_h <= 0) return false;
  if ((size_t)new_w > 16384 || (size_t)new_h > 16384) return false;

  for (int i = 0; i < doc->layer_count; i++) {
    if (!layer_crop_expand(doc->layers[i], doc->canvas_w, doc->canvas_h,
                           x0, y0, new_w, new_h))
      return false;
  }

  free(doc->composite_buf);
  doc->composite_buf = malloc((size_t)new_w * new_h * 4);

  doc->canvas_w     = new_w;
  doc->canvas_h     = new_h;
  doc->pixels       = doc->layers[doc->active_layer]->pixels;
  doc->canvas_dirty = true;
  doc->modified     = true;
  doc->sel_active   = false;
  return true;
}

// Extract the current selection into a float buffer and clear that region.
// Enters "move mode": the caller should track float_pos deltas and call
// canvas_commit_move() when the drag ends.
void canvas_begin_move(canvas_doc_t *doc, uint32_t bg) {
  if (!doc || !doc->sel_active || doc->sel_moving) return;
  int x0, y0, x1, y1;
  if (!selection_bounds(doc, &x0, &y0, &x1, &y1)) return;
  int w = x1 - x0 + 1;
  int h = y1 - y0 + 1;
  uint8_t *buf = malloc((size_t)w * h * 4);
  if (!buf) return;
  // Extract pixels
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      uint32_t c = canvas_get_pixel(doc, x0 + col, y0 + row);
      uint8_t *p = buf + ((size_t)row * w + col) * 4;
      p[0]=COLOR_R(c); p[1]=COLOR_G(c); p[2]=COLOR_B(c); p[3]=COLOR_A(c);
    }
  }
  // Clear the region from canvas
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
      canvas_set_pixel_direct(doc, x, y, bg);
  doc->float_pixels  = buf;
  doc->float_w       = w;
  doc->float_h       = h;
  doc->float_pos     = (ipoint16_t){x0, y0};
  doc->sel_moving    = true;
}

// Paste float_pixels back at float_pos, update selection bounds, end move.
void canvas_commit_move(canvas_doc_t *doc) {
  if (!doc || !doc->sel_moving) return;
  int dx = doc->float_pos.x;
  int dy = doc->float_pos.y;
  int w  = doc->float_w;
  int h  = doc->float_h;
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      const uint8_t *p = doc->float_pixels + ((size_t)row * w + col) * 4;
      canvas_set_pixel_direct(doc, dx + col, dy + row, MAKE_COLOR(p[0], p[1], p[2], p[3]));
    }
  }
  // Update selection to the new position
  doc->sel_start  = (ipoint16_t){dx, dy};
  doc->sel_end    = (ipoint16_t){dx + w - 1, dy + h - 1};
  // Release float resources including the GL texture overlay.
  if (doc->float_tex) {
    glDeleteTextures(1, &doc->float_tex);
    doc->float_tex = 0;
  }
  free(doc->float_pixels);
  doc->float_pixels = NULL;
  doc->float_w      = 0;
  doc->float_h      = 0;
  doc->sel_moving   = false;
}

// ============================================================
// Image operations: flip, invert, resize
// ============================================================

// Flip canvas pixels horizontally (mirror left-right).
void canvas_flip_h(canvas_doc_t *doc) {
  if (!doc) return;
  int w = doc->canvas_w, h = doc->canvas_h;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w / 2; x++) {
      uint8_t *l = doc->pixels + ((size_t)y * w + x) * 4;
      uint8_t *r = doc->pixels + ((size_t)y * w + (w - 1 - x)) * 4;
      uint8_t tmp[4];
      memcpy(tmp, l, 4);
      memcpy(l, r, 4);
      memcpy(r, tmp, 4);
    }
  }
  doc->canvas_dirty = true;
  doc->modified     = true;
}

// Flip canvas pixels vertically (mirror top-bottom).
void canvas_flip_v(canvas_doc_t *doc) {
  if (!doc) return;
  int w = doc->canvas_w, h = doc->canvas_h;
  size_t row_bytes = (size_t)w * 4;
  uint8_t *tmp = malloc(row_bytes);
  if (!tmp) return;
  for (int y = 0; y < h / 2; y++) {
    uint8_t *top = doc->pixels + (size_t)y * row_bytes;
    uint8_t *bot = doc->pixels + (size_t)(h - 1 - y) * row_bytes;
    memcpy(tmp, top, row_bytes);
    memcpy(top, bot, row_bytes);
    memcpy(bot, tmp, row_bytes);
  }
  free(tmp);
  doc->canvas_dirty = true;
  doc->modified     = true;
}

// Invert all pixel colors (complement R, G, B; leave alpha unchanged).
void canvas_invert_colors(canvas_doc_t *doc) {
  if (!doc) return;
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  for (size_t i = 0; i < n; i++) {
    uint8_t *p = doc->pixels + i * 4;
    p[0] = (uint8_t)(255 - p[0]);
    p[1] = (uint8_t)(255 - p[1]);
    p[2] = (uint8_t)(255 - p[2]);
    // alpha unchanged
  }
  doc->canvas_dirty = true;
  doc->modified     = true;
}

// Resize the canvas to new_w x new_h.
// All layer pixel buffers are resized. Existing pixels are preserved
// at the top-left corner; any new area is filled with opaque white.
// The GL texture is invalidated so it will be re-created on the next paint.
// Returns true on success, false if an allocation fails (canvas may be partially
// resized in that case; callers should treat it as a fatal document error).
bool canvas_resize(canvas_doc_t *doc, int new_w, int new_h) {
  if (!doc || new_w <= 0 || new_h <= 0) return false;
  if (new_w == doc->canvas_w && new_h == doc->canvas_h) return true;
  if ((size_t)new_w > 16384 || (size_t)new_h > 16384) return false;

  for (int i = 0; i < doc->layer_count; i++) {
    if (!layer_crop_expand(doc->layers[i], doc->canvas_w, doc->canvas_h,
                           0, 0, new_w, new_h))
      return false;
  }

  free(doc->composite_buf);
  doc->composite_buf = malloc((size_t)new_w * new_h * 4);
  // If this allocation fails, canvas_upload will retry and skip rendering
  // until it succeeds.  The document's layer data is already valid.

  doc->canvas_w     = new_w;
  doc->canvas_h     = new_h;
  doc->pixels       = doc->layers[doc->active_layer]->pixels;
  doc->canvas_dirty = true;
  doc->modified     = true;
  return true;
}

// ============================================================
// PNG I/O (stb_image)
// ============================================================

bool png_save(const char *path, const canvas_doc_t *doc) {
  // Composite all layers before saving.
  size_t sz = (size_t)doc->canvas_w * doc->canvas_h * 4;
  uint8_t *comp = malloc(sz);
  if (!comp) return false;
  canvas_composite(doc, comp);
  bool ok = save_image_png(path, comp, doc->canvas_w, doc->canvas_h);
  free(comp);
  return ok;
}

// ============================================================
// Canvas GL texture
// ============================================================

void canvas_upload(canvas_doc_t *doc) {
  if (!doc) return;
  bool need_upload = doc->canvas_dirty;
  for (int i = 0; i < doc->layer_count && !need_upload; i++) {
    if (!doc->layers[i]->tex)
      need_upload = true;
  }

  if (need_upload) {
    for (int i = 0; i < doc->layer_count; i++)
      layer_upload_texture(doc, doc->layers[i]);
    doc->canvas_dirty = false;
  }
}
