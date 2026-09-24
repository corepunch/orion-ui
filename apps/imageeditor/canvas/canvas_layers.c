// Canvas layer management: creation, deletion, compositing, masks

#include "imageeditor.h"

// ============================================================
// Layer management helpers
// ============================================================

// Allocate a new layer with a transparent pixel buffer.
static layer_t *layer_new(int w, int h, const char *name) {
  layer_t *lay = calloc(1, sizeof(layer_t));
  if (!lay) return NULL;
  lay->pixels = malloc((size_t)w * h * DOC_BPP);
  if (!lay->pixels) { free(lay); return NULL; }
  memset(lay->pixels, 0x00, (size_t)w * h * DOC_BPP);
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
static uint8_t layer_empty_value(const canvas_doc_t *doc, int layer) {
#if IMAGEEDITOR_INDEXED
  if (pencil_has_layers(doc)) {
    if (layer == IE_LAYER_PENCIL) return 0;
    if (layer == IE_LAYER_BG) return (uint8_t)canvas_nearest_palette_index(doc, IE_PAPER_COLOR);
  }
  return (uint8_t)doc->ipal.transparent;
#else
  (void)doc; (void)layer;
  return 0;
#endif
}

static bool layer_crop_expand(layer_t *lay, int old_w, int old_h,
                               int src_x, int src_y, int new_w, int new_h, uint8_t empty) {
  uint8_t *buf = malloc((size_t)new_w * new_h * DOC_BPP);
  if (!buf) return false;
  memset(buf, empty, (size_t)new_w * new_h * DOC_BPP);

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
      const uint8_t *srow = lay->pixels + ((size_t)(iy0 + r) * old_w + ix0) * DOC_BPP;
      uint8_t       *drow_ptr = buf + ((size_t)(drow + r) * new_w + dcol) * DOC_BPP;
      memcpy(drow_ptr, srow, (size_t)iw * DOC_BPP);
    }
  }
  free(lay->pixels);
  lay->pixels = buf;
  if (lay->tex) {
    glDeleteTextures(1, &lay->tex);
    lay->tex = 0;
  }
  lay->preview.active = false;
  return true;
}

static void layer_clear_preview_one(layer_t *lay) {
  if (!lay) return;
  lay->preview.active = false;
  lay->preview.effect = IE_RENDER_EFFECT_COPY;
  memset(&lay->preview.params, 0, sizeof(lay->preview.params));
}

// ============================================================
// Public layer management API
// ============================================================

bool doc_add_layer_filled(canvas_doc_t *doc, uint32_t fill_color) {
  if (!doc || doc->layer.count >= LAYER_MAX) return false;

  char name[64];
  if (doc->layer.count == 0)
    strncpy(name, "Layer 1", sizeof(name) - 1);
  else
    snprintf(name, sizeof(name), "Layer %d", doc->layer.count + 1);

  layer_t *lay = layer_new(doc->canvas_w, doc->canvas_h, name);
  if (!lay) return false;

  // Fill with the requested color.
#if IMAGEEDITOR_INDEXED
  // In indexed mode, map the fill color to the nearest palette entry.
  // Transparent fill uses the designated transparent index; opaque fill
  // uses nearest_palette_index() so it never accidentally lands on index 0
  // (which is always the transparent slot).
  uint8_t pidx = (uint8_t)canvas_nearest_palette_index(doc, fill_color);
  memset(lay->pixels, pidx, (size_t)doc->canvas_w * (size_t)doc->canvas_h);
  (void)fill_color;
#else
  // Fill with the requested color using 4-byte writes for efficiency.
  // malloc() returns sufficiently aligned memory for uint32_t access.
  size_t npx = (size_t)doc->canvas_w * doc->canvas_h;
  uint32_t *dst = (uint32_t *)lay->pixels;
  for (size_t i = 0; i < npx; i++)
    dst[i] = fill_color;
#endif

  layer_t **nl = realloc(doc->layer.stack, sizeof(layer_t *) * (doc->layer.count + 1));
  if (!nl) { layer_free_one(lay); return false; }
  doc->layer.stack = nl;
  doc->layer.stack[doc->layer.count] = lay;
  doc->layer.count++;
  doc->layer.active = doc->layer.count - 1;
  doc->pixels = doc->layer.stack[doc->layer.active]->pixels;
  if (doc->layer.count > 1) {
    doc->canvas_dirty = true;
    doc->modified = true;
  }
  return true;
}

bool doc_add_layer(canvas_doc_t *doc) {
  // Default fill: transparent, so the document background stays separate.
  return doc_add_layer_filled(doc, MAKE_COLOR(0x00, 0x00, 0x00, 0x00));
}

bool canvas_fill_active_layer(canvas_doc_t *doc, uint32_t fill_color) {
  if (!doc || doc->layer.count <= 0 || doc->layer.active < 0 ||
      doc->layer.active >= doc->layer.count)
    return false;

  layer_t *lay = doc->layer.stack[doc->layer.active];
  if (!lay || !lay->pixels) return false;

#if IMAGEEDITOR_INDEXED
  uint8_t value = (uint8_t)canvas_nearest_palette_index(doc, fill_color);
  if (pencil_has_layers(doc) && doc->layer.active == IE_LAYER_PENCIL)
    value = fill_color == IE_PAPER_COLOR ? 0 : COLOR_A(fill_color);
  uint8_t *dst = lay->pixels;
#else
  uint32_t value = fill_color;
  uint32_t *dst = (uint32_t *)lay->pixels;
#endif
  for (int y = 0; y < doc->canvas_h; y++) {
    for (int x = 0; x < doc->canvas_w; x++) {
      if (doc->sel.active && !canvas_in_selection(doc, x, y)) continue;
      dst[(size_t)y * doc->canvas_w + x] = value;
    }
  }

  doc->pixels = lay->pixels;
  doc->canvas_dirty = true;
  doc->modified = true;
  return true;
}

bool doc_delete_layer(canvas_doc_t *doc) {
  if (!doc || doc->layer.count <= 1) return false;
  int i = doc->layer.active;
  layer_free_one(doc->layer.stack[i]);
  memmove(&doc->layer.stack[i], &doc->layer.stack[i + 1],
          sizeof(layer_t *) * (doc->layer.count - i - 1));
  doc->layer.count--;
  if (doc->layer.active >= doc->layer.count)
    doc->layer.active = doc->layer.count - 1;
  doc->pixels = doc->layer.stack[doc->layer.active]->pixels;
  doc->layer.editing_mask = false;
  doc->canvas_dirty = true;
  doc->modified = true;
  return true;
}

bool doc_duplicate_layer(canvas_doc_t *doc) {
  if (!doc || doc->layer.count >= LAYER_MAX) return false;
  const layer_t *src = doc->layer.stack[doc->layer.active];
  size_t px_sz = (size_t)doc->canvas_w * doc->canvas_h * DOC_BPP;

  layer_t *dup = calloc(1, sizeof(layer_t));
  if (!dup) return false;
  dup->pixels = malloc(px_sz);
  if (!dup->pixels) { free(dup); return false; }
  memcpy(dup->pixels, src->pixels, px_sz);
  dup->visible = src->visible;
  dup->opacity = src->opacity;
  dup->blend_mode = src->blend_mode;
  snprintf(dup->name, sizeof(dup->name), "%s copy", src->name);

  int ins = doc->layer.active + 1;
  layer_t **nl = realloc(doc->layer.stack, sizeof(layer_t *) * (doc->layer.count + 1));
  if (!nl) { free(dup->pixels); free(dup); return false; }
  doc->layer.stack = nl;
  memmove(&doc->layer.stack[ins + 1], &doc->layer.stack[ins],
          sizeof(layer_t *) * (doc->layer.count - ins));
  doc->layer.stack[ins] = dup;
  doc->layer.count++;
  doc->layer.active = ins;
  doc->pixels = doc->layer.stack[doc->layer.active]->pixels;
  doc->canvas_dirty = true;
  doc->modified = true;
  return true;
}

void doc_set_active_layer(canvas_doc_t *doc, int idx) {
  if (!doc || idx < 0 || idx >= doc->layer.count) return;
  doc->layer.active = idx;
  doc->pixels = doc->layer.stack[idx]->pixels;
  doc->layer.editing_mask = false;
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
  if (doc->layer.mask_only_view == enabled) return;
  doc->layer.mask_only_view = enabled;
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
  if (!doc || doc->layer.active >= doc->layer.count - 1) return;
  int i = doc->layer.active;
  layer_t *tmp = doc->layer.stack[i];
  doc->layer.stack[i] = doc->layer.stack[i + 1];
  doc->layer.stack[i + 1] = tmp;
  doc->layer.active = i + 1;
  doc->canvas_dirty = true;
  doc->modified = true;
}

void doc_move_layer_down(canvas_doc_t *doc) {
  if (!doc || doc->layer.active == 0) return;
  int i = doc->layer.active;
  layer_t *tmp = doc->layer.stack[i];
  doc->layer.stack[i] = doc->layer.stack[i - 1];
  doc->layer.stack[i - 1] = tmp;
  doc->layer.active = i - 1;
  doc->canvas_dirty = true;
  doc->modified = true;
}

void doc_merge_down(canvas_doc_t *doc) {
  if (!doc || doc->layer.active == 0 || doc->layer.count < 2) return;
#if IMAGEEDITOR_INDEXED
  // Indexed mode is always single-layer; merge-down is a no-op.
  return;
#else
  int top_idx = doc->layer.active;
  int bot_idx = top_idx - 1;
  const layer_t *top = doc->layer.stack[top_idx];
  layer_t       *bot = doc->layer.stack[bot_idx];
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

  layer_free_one(doc->layer.stack[top_idx]);
  memmove(&doc->layer.stack[top_idx], &doc->layer.stack[top_idx + 1],
          sizeof(layer_t *) * (doc->layer.count - top_idx - 1));
  doc->layer.count--;
  doc->layer.active = bot_idx;
  doc->pixels = doc->layer.stack[doc->layer.active]->pixels;
  doc->canvas_dirty = true;
  doc->modified = true;
#endif
}

void doc_flatten(canvas_doc_t *doc) {
  if (!doc || doc->layer.count < 1) return;
  if (doc->layer.count == 1) {
    // Nothing to flatten.
    return;
  }

#if IMAGEEDITOR_INDEXED
  // Indexed mode is always single-layer; flatten is a no-op.
  return;
#else
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
  strncpy(bg->name, "Layer 1", sizeof(bg->name) - 1);

  // All allocations succeeded — now free the old stack.
  for (int i = 0; i < doc->layer.count; i++)
    layer_free_one(doc->layer.stack[i]);
  free(doc->layer.stack);

  doc->layer.stack       = nl;
  doc->layer.stack[0]    = bg;
  doc->layer.count  = 1;
  doc->layer.active = 0;
  doc->pixels       = bg->pixels;
  doc->layer.editing_mask = false;
  doc->canvas_dirty = true;
  doc->modified     = true;
#endif
}

void doc_free_layers(canvas_doc_t *doc) {
  if (!doc) return;
  canvas_stroke_cancel(doc);
  for (int i = 0; i < doc->layer.count; i++)
    layer_free_one(doc->layer.stack[i]);
  free(doc->layer.stack);
  doc->layer.stack       = NULL;
  doc->layer.count  = 0;
  doc->layer.active = 0;
  doc->pixels       = NULL;
}

// ============================================================
// Alpha editing / mask operations
// ============================================================

bool layer_add_mask_ex(canvas_doc_t *doc, int idx, int fill_mode);

bool layer_add_mask(canvas_doc_t *doc, int idx) {
  return layer_add_mask_ex(doc, idx, MASK_EXTRACT_WHITE);
}

#if !IMAGEEDITOR_INDEXED
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
#endif

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
  if (!doc || idx < 0 || idx >= doc->layer.count) return;
  layer_t *lay = doc->layer.stack[idx];
  if (!lay) return;
  lay->blend_mode = (uint8_t)CLAMP((int)mode, 0, (int)LAYER_BLEND_COUNT - 1);
  doc->canvas_dirty = true;
  doc->modified = true;
  if (doc->canvas_win)
    invalidate_window(doc->canvas_win);
}

void layer_clear_preview_effect(canvas_doc_t *doc, int idx) {
  if (!doc || idx < 0 || idx >= doc->layer.count) return;
  layer_clear_preview_one(doc->layer.stack[idx]);
  if (doc->canvas_win)
    invalidate_window(doc->canvas_win);
}

bool layer_set_preview_effect(canvas_doc_t *doc, int idx,
                              imageeditor_render_effect_t effect,
                              const ui_render_effect_params_t *params) {
  if (!doc || idx < 0 || idx >= doc->layer.count) return false;
  layer_t *lay = doc->layer.stack[idx];
  if (!lay) return false;
  lay->preview.effect = effect;
  if (params)
    lay->preview.params = *params;
  else
    memset(&lay->preview.params, 0, sizeof(lay->preview.params));
  lay->preview.active = true;
  if (doc->canvas_win)
    invalidate_window(doc->canvas_win);
  return true;
}

bool layer_commit_preview_effect(canvas_doc_t *doc, int idx) {
  if (!doc || idx < 0 || idx >= doc->layer.count) return false;
  layer_t *lay = doc->layer.stack[idx];
  if (!lay) return false;
  if (!lay->preview.active) return true;
#if IMAGEEDITOR_INDEXED
  // GL-based effects not supported in indexed mode.
  layer_clear_preview_one(lay);
  return false;
#else
  size_t sz = (size_t)doc->canvas_w * doc->canvas_h * 4;
  uint8_t *buf = malloc(sz);
  if (!buf) return false;
  uint32_t baked_tex = 0;
  if (!imageeditor_bake_texture_effect((int)lay->tex, doc->canvas_w, doc->canvas_h,
                                       lay->preview.effect, &lay->preview.params, &baked_tex)) {
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
#endif
}

bool layer_add_mask_ex(canvas_doc_t *doc, int idx, int fill_mode) {
  if (!doc || idx < 0 || idx >= doc->layer.count) return false;
#if IMAGEEDITOR_INDEXED
  // Indexed images have no alpha channel — masks are not supported.
  (void)fill_mode;
  return false;
#else
  layer_t *lay = doc->layer.stack[idx];
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  if (fill_mode == MASK_EXTRACT_GRAYSCALE) {
    fill_alpha_from_layer_gray(doc, lay);
  } else {
    fill_alpha_gray_value(lay->pixels, n, fill_mode_to_alpha(fill_mode));
  }
  doc->layer.editing_mask = true;
  doc->canvas_dirty = true;
  doc->modified     = true;
  return true;
#endif
}

void layer_apply_mask(canvas_doc_t *doc, int idx) {
  if (!doc || idx < 0 || idx >= doc->layer.count) return;
  doc->canvas_dirty = true;
  doc->modified     = true;
  doc->layer.editing_mask = false;
}

void layer_remove_mask(canvas_doc_t *doc, int idx) {
  if (!doc || idx < 0 || idx >= doc->layer.count) return;
#if !IMAGEEDITOR_INDEXED
  layer_t *lay = doc->layer.stack[idx];
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  for (size_t i = 0; i < n; i++)
    lay->pixels[i * 4 + 3] = 255;
#endif
  doc->canvas_dirty = true;
  doc->modified     = true;
  doc->layer.editing_mask = false;
}

// Open the active layer's alpha channel as a new document.
// Not supported in indexed mode (no alpha channel in palette indices).
canvas_doc_t *canvas_extract_mask(canvas_doc_t *doc) {
#if IMAGEEDITOR_INDEXED
  (void)doc;
  return NULL;
#else
  if (!doc || !g_app || doc->layer.count == 0) return NULL;
  layer_t *lay = doc->layer.stack[doc->layer.active];
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
#endif
}

// ============================================================
// Canvas pixel operations
// ============================================================

#if IMAGEEDITOR_INDEXED
// Find the palette entry in doc->ipal that is nearest to the given RGBA color.
// Uses Manhattan distance in RGBA space.  Returns the transparent index if the
// color has alpha == 0.  Returns ipal.transparent if the palette is empty.
int canvas_nearest_palette_index(const canvas_doc_t *doc, uint32_t color) {
  if (COLOR_A(color) == 0)
    return doc->ipal.transparent;
  if (doc->ipal.count <= 0)
    return doc->ipal.transparent;

  int best_idx = 0;
  int best_dist = INT_MAX;
  int r = COLOR_R(color), g = COLOR_G(color), b = COLOR_B(color), a = COLOR_A(color);
  for (int i = 0; i < doc->ipal.count && i < 256; i++) {
    if (i == doc->ipal.transparent) continue;  // skip transparent slot
    uint32_t e = doc->ipal.entries[i];
    int dr = abs(r - (int)COLOR_R(e));
    int dg = abs(g - (int)COLOR_G(e));
    int db = abs(b - (int)COLOR_B(e));
    int da = abs(a - (int)COLOR_A(e));
    int dist = dr + dg + db + da;
    if (dist < best_dist) {
      best_dist = dist;
      best_idx  = i;
    }
  }
  return best_idx;
}
#endif
