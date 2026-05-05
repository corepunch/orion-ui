// Canvas operations: pixel drawing, PNG I/O, GL texture management

#include "imageeditor.h"

// ============================================================
// Layer management helpers
// ============================================================

// Allocate a new layer with a transparent pixel buffer.
static layer_t *layer_new(int w, int h, const char *name) {
  layer_t *lay = calloc(1, sizeof(layer_t));
  if (!lay) return NULL;
  lay->pixels = malloc((size_t)w * h * 4);
  if (!lay->pixels) { free(lay); return NULL; }
  memset(lay->pixels, 0x00, (size_t)w * h * 4);
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
  memset(buf, 0x00, (size_t)new_w * new_h * 4);

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
  lay->preview.active = false;
  return true;
}

// Composite all visible layers into dst (canvas_w * canvas_h * 4 RGBA).
// The result preserves alpha so the canvas can be drawn over a checkerboard.
static void canvas_composite(const canvas_doc_t *doc, uint8_t *dst) {
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  memset(dst, 0x00, n * 4);

  for (int li = 0; li < doc->layer.count; li++) {
    const layer_t *lay = doc->layer.stack[li];
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

static void canvas_composite_over_bg(const canvas_doc_t *doc, uint8_t *rgba) {
  if (!doc || !rgba) return;
  if (!doc->background.show) return;

  uint8_t bg_r = COLOR_R(doc->background.color);
  uint8_t bg_g = COLOR_G(doc->background.color);
  uint8_t bg_b = COLOR_B(doc->background.color);
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;

  for (size_t i = 0; i < n; i++) {
    uint8_t *p = rgba + i * 4;
    uint32_t sa = p[3];
    if (sa == 0) {
      p[0] = bg_r;
      p[1] = bg_g;
      p[2] = bg_b;
      p[3] = 255;
      continue;
    }
    if (sa == 255) {
      continue;
    }

    uint32_t inv = 255 - sa;
    p[0] = (uint8_t)((p[0] * sa + bg_r * inv + 127) / 255);
    p[1] = (uint8_t)((p[1] * sa + bg_g * inv + 127) / 255);
    p[2] = (uint8_t)((p[2] * sa + bg_b * inv + 127) / 255);
    p[3] = 255;
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
  lay->preview.active = false;
  lay->preview.effect = UI_RENDER_EFFECT_COPY;
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

  // Fill with the requested color using 4-byte writes for efficiency.
  // malloc() returns sufficiently aligned memory for uint32_t access.
  size_t npx = (size_t)doc->canvas_w * doc->canvas_h;
  uint32_t *dst = (uint32_t *)lay->pixels;
  for (size_t i = 0; i < npx; i++)
    dst[i] = fill_color;

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
}

void doc_flatten(canvas_doc_t *doc) {
  if (!doc || doc->layer.count < 1) return;
  if (doc->layer.count == 1) {
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
}

void doc_free_layers(canvas_doc_t *doc) {
  if (!doc) return;
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

static uint8_t color_to_gray(uint32_t c) {
  return (uint8_t)((COLOR_R(c) * 77 + COLOR_G(c) * 150 + COLOR_B(c) * 29) >> 8);
}

void canvas_clear_selection_mask(canvas_doc_t *doc) {
  if (!doc) return;
  if (doc->sel.mask.tex) {
    R_DeleteTexture(doc->sel.mask.tex);
    doc->sel.mask.tex = 0;
  }
  free(doc->sel.mask.data);
  doc->sel.mask.data = NULL;
  doc->sel.mask.dirty = false;
  doc->sel.mask.offset = (ipoint16_t){0, 0};
}

static bool selection_mask_bounds_for(const canvas_doc_t *doc, const uint8_t *mask,
                                      int *out_x0, int *out_y0,
                                      int *out_x1, int *out_y1) {
  if (!doc || !mask) return false;
  int x0 = doc->canvas_w, y0 = doc->canvas_h, x1 = -1, y1 = -1;
  for (int y = 0; y < doc->canvas_h; y++) {
    for (int x = 0; x < doc->canvas_w; x++) {
      if (mask[(size_t)y * doc->canvas_w + x] == 255) continue;
      if (x < x0) x0 = x;
      if (y < y0) y0 = y;
      if (x > x1) x1 = x;
      if (y > y1) y1 = y;
    }
  }
  if (x1 < x0 || y1 < y0) return false;
  if (out_x0) *out_x0 = x0;
  if (out_y0) *out_y0 = y0;
  if (out_x1) *out_x1 = x1;
  if (out_y1) *out_y1 = y1;
  return true;
}

static bool canvas_apply_selection_mask(canvas_doc_t *doc, uint8_t *mask,
                                        bool add_to_selection) {
  if (!doc || !mask) {
    free(mask);
    return false;
  }

  if (add_to_selection && doc->sel.active) {
    if (doc->sel.mask.data) {
      for (size_t i = 0, count = (size_t)doc->canvas_w * doc->canvas_h; i < count; i++)
        mask[i] = MIN(mask[i], doc->sel.mask.data[i]);
    } else {
      for (int y = 0; y < doc->canvas_h; y++) {
        for (int x = 0; x < doc->canvas_w; x++) {
          if (canvas_in_selection(doc, x, y))
            mask[(size_t)y * doc->canvas_w + x] = 0;
        }
      }
    }
  }

  int x0, y0, x1, y1;
  if (!selection_mask_bounds_for(doc, mask, &x0, &y0, &x1, &y1)) {
    free(mask);
    doc->sel.active = false;
    canvas_clear_selection_mask(doc);
    return false;
  }

  canvas_clear_selection_mask(doc);
  doc->sel.mask.data = mask;
  doc->sel.mask.dirty = true;
  doc->sel.active = true;
  doc->sel.start = (ipoint16_t){x0, y0};
  doc->sel.end = (ipoint16_t){x1, y1};
  return true;
}

static bool canvas_select_rect_ex(canvas_doc_t *doc, int x0, int y0, int x1, int y1,
                                  bool add_to_selection) {
  if (!doc) return false;
  int left = MIN(x0, x1);
  int top = MIN(y0, y1);
  int right = MAX(x0, x1);
  int bottom = MAX(y0, y1);

  left = MAX(0, left);
  top = MAX(0, top);
  right = MIN(doc->canvas_w - 1, right);
  bottom = MIN(doc->canvas_h - 1, bottom);

  if (left > right || top > bottom) {
    doc->sel.active = false;
    canvas_clear_selection_mask(doc);
    return false;
  }

  size_t count = (size_t)doc->canvas_w * doc->canvas_h;
  uint8_t *mask = malloc(count);
  if (!mask) return false;
  memset(mask, 255, count);

  for (int y = top; y <= bottom; y++) {
    memset(mask + (size_t)y * doc->canvas_w + left, 0,
           (size_t)(right - left + 1));
  }

  return canvas_apply_selection_mask(doc, mask, add_to_selection);
}

bool canvas_select_rect(canvas_doc_t *doc, int x0, int y0, int x1, int y1) {
  return canvas_select_rect_ex(doc, x0, y0, x1, y1, false);
}

bool canvas_select_rect_add(canvas_doc_t *doc, int x0, int y0, int x1, int y1) {
  return canvas_select_rect_ex(doc, x0, y0, x1, y1, true);
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
                              ui_render_effect_t effect,
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

  size_t sz = (size_t)doc->canvas_w * doc->canvas_h * 4;
  uint8_t *buf = malloc(sz);
  if (!buf) return false;
  uint32_t baked_tex = 0;
  if (!bake_texture_effect((int)lay->tex, doc->canvas_w, doc->canvas_h,
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
}

bool layer_add_mask_ex(canvas_doc_t *doc, int idx, int fill_mode) {
  if (!doc || idx < 0 || idx >= doc->layer.count) return false;
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
}

void layer_apply_mask(canvas_doc_t *doc, int idx) {
  if (!doc || idx < 0 || idx >= doc->layer.count) return;
  doc->canvas_dirty = true;
  doc->modified     = true;
  doc->layer.editing_mask = false;
}

void layer_remove_mask(canvas_doc_t *doc, int idx) {
  if (!doc || idx < 0 || idx >= doc->layer.count) return;
  layer_t *lay = doc->layer.stack[idx];
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  for (size_t i = 0; i < n; i++)
    lay->pixels[i * 4 + 3] = 255;
  doc->canvas_dirty = true;
  doc->modified     = true;
  doc->layer.editing_mask = false;
}

// Open the active layer's alpha channel as a new document.
canvas_doc_t *canvas_extract_mask(canvas_doc_t *doc) {
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
  if (doc->layer.editing_mask) {
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
  memset(doc->pixels, 0x00, (size_t)doc->canvas_w * doc->canvas_h * 4);
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

static int color_spread_distance(uint32_t a, uint32_t b) {
  int dr = abs((int)COLOR_R(a) - (int)COLOR_R(b));
  int dg = abs((int)COLOR_G(a) - (int)COLOR_G(b));
  int db = abs((int)COLOR_B(a) - (int)COLOR_B(b));
  int da = abs((int)COLOR_A(a) - (int)COLOR_A(b));
  return MAX(MAX(dr, dg), MAX(db, da));
}

static bool canvas_magic_wand_select_ex(canvas_doc_t *doc, int sx, int sy,
                                        int spread, bool antialias,
                                        bool add_to_selection) {
  if (!doc || !canvas_in_bounds(doc, sx, sy)) return false;

  size_t count = (size_t)doc->canvas_w * (size_t)doc->canvas_h;
  if (count == 0 || count > 64 * 1024 * 1024) return false;

  uint8_t *mask = malloc(count);
  typedef struct { int x, y; } pt_t;
  pt_t *queue = malloc(sizeof(pt_t) * count);
  if (!mask || !queue) {
    free(mask);
    free(queue);
    return false;
  }
  memset(mask, 255, count);

  uint32_t target = canvas_get_pixel(doc, sx, sy);
  spread = CLAMP(spread, 0, 255);
  size_t head = 0, tail = 0;
  mask[(size_t)sy * doc->canvas_w + sx] = 0;
  queue[tail++] = (pt_t){sx, sy};
  int x0 = sx, y0 = sy, x1 = sx, y1 = sy;

  while (head < tail) {
    pt_t cur = queue[head++];
    int nx[4] = {cur.x + 1, cur.x - 1, cur.x,     cur.x};
    int ny[4] = {cur.y,     cur.y,     cur.y + 1, cur.y - 1};
    for (int i = 0; i < 4; i++) {
      if (!canvas_in_bounds(doc, nx[i], ny[i])) continue;
      size_t idx = (size_t)ny[i] * doc->canvas_w + nx[i];
      if (mask[idx] == 0) continue;
      if (color_spread_distance(canvas_get_pixel(doc, nx[i], ny[i]), target) > spread)
        continue;
      mask[idx] = 0;
      queue[tail++] = (pt_t){nx[i], ny[i]};
      if (nx[i] < x0) x0 = nx[i];
      if (ny[i] < y0) y0 = ny[i];
      if (nx[i] > x1) x1 = nx[i];
      if (ny[i] > y1) y1 = ny[i];
    }
  }

  if (antialias) {
    uint8_t *soft = malloc(count);
    if (soft) {
      memcpy(soft, mask, count);
      for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
          size_t idx = (size_t)y * doc->canvas_w + x;
          if (mask[idx] != 0) continue;
          for (int oy = -1; oy <= 1; oy++) {
            for (int ox = -1; ox <= 1; ox++) {
              int xx = x + ox, yy = y + oy;
              if (!canvas_in_bounds(doc, xx, yy)) continue;
              size_t nidx = (size_t)yy * doc->canvas_w + xx;
              int dist = color_spread_distance(canvas_get_pixel(doc, xx, yy), target);
              if (soft[nidx] != 0 && dist <= spread + 12) {
                soft[nidx] = (dist <= spread) ? 0 : 128;
                if (xx < x0) x0 = xx;
                if (yy < y0) y0 = yy;
                if (xx > x1) x1 = xx;
                if (yy > y1) y1 = yy;
              }
            }
          }
        }
      }
      free(mask);
      mask = soft;
    }
  }

  free(queue);
  return canvas_apply_selection_mask(doc, mask, add_to_selection);
}

bool canvas_magic_wand_select(canvas_doc_t *doc, int sx, int sy,
                              int spread, bool antialias) {
  return canvas_magic_wand_select_ex(doc, sx, sy, spread, antialias, false);
}

bool canvas_magic_wand_select_add(canvas_doc_t *doc, int sx, int sy,
                                  int spread, bool antialias) {
  return canvas_magic_wand_select_ex(doc, sx, sy, spread, antialias, true);
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

typedef enum {
  DRAG_ALIAS_NONE = 0,
  DRAG_ALIAS_45_DEGREES,
  DRAG_ALIAS_SQUARE,
} drag_alias_t;

typedef struct {
  int          tool_id;
  uint32_t     mods;
  drag_alias_t alias;
} tool_drag_alias_t;

static const tool_drag_alias_t kToolDragAliases[] = {
  { ID_TOOL_LINE,         AX_MOD_SHIFT, DRAG_ALIAS_45_DEGREES },
  { ID_TOOL_RECT,         AX_MOD_SHIFT, DRAG_ALIAS_SQUARE },
  { ID_TOOL_ELLIPSE,      AX_MOD_SHIFT, DRAG_ALIAS_SQUARE },
  { ID_TOOL_ROUNDED_RECT, AX_MOD_SHIFT, DRAG_ALIAS_SQUARE },
  { ID_TOOL_SELECT,       AX_MOD_SHIFT, DRAG_ALIAS_SQUARE },
};

static drag_alias_t tool_drag_alias_for(int tool_id, uint32_t mods) {
  for (size_t i = 0; i < sizeof(kToolDragAliases) / sizeof(kToolDragAliases[0]); i++) {
    const tool_drag_alias_t *a = &kToolDragAliases[i];
    if (a->tool_id == tool_id && (mods & a->mods) == a->mods)
      return a->alias;
  }
  return DRAG_ALIAS_NONE;
}

void canvas_constrain_tool_drag(int tool_id, uint32_t mods,
                                int x0, int y0, int *x1, int *y1) {
  if (!x1 || !y1) return;
  int dx = *x1 - x0;
  int dy = *y1 - y0;

  switch (tool_drag_alias_for(tool_id, mods)) {
    case DRAG_ALIAS_45_DEGREES:
      if (abs(dx) > abs(dy) * 2) {
        dy = 0;
      } else if (abs(dy) > abs(dx) * 2) {
        dx = 0;
      } else {
        int s = MAX(abs(dx), abs(dy));
        dx = (dx < 0) ? -s : s;
        dy = (dy < 0) ? -s : s;
      }
      *x1 = x0 + dx;
      *y1 = y0 + dy;
      break;
    case DRAG_ALIAS_SQUARE: {
      int s = MIN(abs(dx), abs(dy));
      *x1 = x0 + ((dx < 0) ? -s : s);
      *y1 = y0 + ((dy < 0) ? -s : s);
      break;
    }
    case DRAG_ALIAS_NONE:
    default:
      break;
  }
}

// Save pixel snapshot before starting a shape drag (no undo push yet)
void canvas_shape_begin(canvas_doc_t *doc, int cx, int cy) {
  size_t sz = (size_t)doc->canvas_w * doc->canvas_h * 4;
  if (!doc->shape.snapshot) {
    doc->shape.snapshot = malloc(sz);
  }
  if (doc->shape.snapshot) {
    memcpy(doc->shape.snapshot, doc->pixels, sz);
  }
  doc->shape.start.x = cx;
  doc->shape.start.y = cy;
}

// Restore snapshot and draw a preview of the current shape without pushing undo.
// shift_held constrains the shape (45° line, square, circle).
void canvas_shape_preview(canvas_doc_t *doc, int x0, int y0, int x1, int y1,
                          int tool, bool filled, uint32_t fg, uint32_t bg, bool shift_held) {
  // Restore snapshot
  if (doc->shape.snapshot) {
    memcpy(doc->pixels, doc->shape.snapshot, (size_t)doc->canvas_w * doc->canvas_h * 4);
    doc->canvas_dirty = true;
  }
  canvas_constrain_tool_drag(tool, shift_held ? AX_MOD_SHIFT : 0, x0, y0, &x1, &y1);
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
  if (doc->sel.mask.data) {
    bool any = false;
    *x0 = doc->canvas_w;
    *y0 = doc->canvas_h;
    *x1 = -1;
    *y1 = -1;
    for (int y = 0; y < doc->canvas_h; y++) {
      for (int x = 0; x < doc->canvas_w; x++) {
        if (doc->sel.mask.data[(size_t)y * doc->canvas_w + x] == 255) continue;
        int sx = x + doc->sel.mask.offset.x;
        int sy = y + doc->sel.mask.offset.y;
        if (!canvas_in_bounds(doc, sx, sy)) continue;
        if (sx < *x0) *x0 = sx;
        if (sy < *y0) *y0 = sy;
        if (sx > *x1) *x1 = sx;
        if (sy > *y1) *y1 = sy;
        any = true;
      }
    }
    return any;
  }
  *x0 = MIN(doc->sel.start.x, doc->sel.end.x);
  *y0 = MIN(doc->sel.start.y, doc->sel.end.y);
  *x1 = MAX(doc->sel.start.x, doc->sel.end.x);
  *y1 = MAX(doc->sel.start.y, doc->sel.end.y);
  // Clamp to canvas bounds so callers are safe against out-of-range coords.
  if (*x0 < 0) *x0 = 0;
  if (*y0 < 0) *y0 = 0;
  if (*x1 >= doc->canvas_w) *x1 = doc->canvas_w - 1;
  if (*y1 >= doc->canvas_h) *y1 = doc->canvas_h - 1;
  return (*x0 <= *x1 && *y0 <= *y1);
}

// Copy the selected region into the app clipboard.
void canvas_copy_selection(canvas_doc_t *doc) {
  if (!doc || !doc->sel.active || !g_app) return;
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
      if (canvas_in_selection(doc, x0 + col, y0 + row)) {
        p[0]=COLOR_R(c); p[1]=COLOR_G(c); p[2]=COLOR_B(c); p[3]=COLOR_A(c);
      } else {
        memset(p, 0, 4);
      }
    }
  }
  free(g_app->clipboard);
  g_app->clipboard   = buf;
  g_app->clipboard_size.w = w;
  g_app->clipboard_size.h = h;
}

// Fill the selected region with fill_color.
void canvas_clear_selection(canvas_doc_t *doc, uint32_t fill) {
  if (!doc || !doc->sel.active) return;
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
  int w = g_app->clipboard_size.w;
  int h = g_app->clipboard_size.h;
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      const uint8_t *p = g_app->clipboard + ((size_t)row * w + col) * 4;
      canvas_set_pixel_direct(doc, col, row, MAKE_COLOR(p[0], p[1], p[2], p[3]));
    }
  }
  // Select the pasted region (clamped to canvas bounds)
  int sel_x1 = w - 1;
  int sel_y1 = h - 1;
  if (sel_x1 >= doc->canvas_w) sel_x1 = doc->canvas_w - 1;
  if (sel_y1 >= doc->canvas_h) sel_y1 = doc->canvas_h - 1;
  canvas_select_rect(doc, 0, 0, sel_x1, sel_y1);
}

// Select the entire canvas.
void canvas_select_all(canvas_doc_t *doc) {
  if (!doc) return;
  canvas_select_rect(doc, 0, 0, doc->canvas_w - 1, doc->canvas_h - 1);
}

// Clear selection (no-op on pixels).
void canvas_deselect(canvas_doc_t *doc) {
  if (!doc) return;
  // Commit any in-progress move before deselecting.
  if (doc->sel.move.active) canvas_commit_move(doc);
  doc->sel.active = false;
  canvas_clear_selection_mask(doc);
}

static bool selection_modify_mask_gpu(canvas_doc_t *doc, int amount, bool expand) {
  if (!g_ui_runtime.running || !doc || !doc->sel.active || amount <= 0)
    return false;

  size_t count = (size_t)doc->canvas_w * doc->canvas_h;
  uint8_t *src = doc->sel.mask.data;
  uint8_t *owned_src = NULL;
  if (!src) {
    int x0, y0, x1, y1;
    if (!selection_bounds(doc, &x0, &y0, &x1, &y1)) return false;
    owned_src = malloc(count);
    if (!owned_src) return false;
    memset(owned_src, 255, count);
    for (int y = y0; y <= y1; y++) {
      memset(owned_src + (size_t)y * doc->canvas_w + x0, 0,
             (size_t)(x1 - x0 + 1));
    }
    src = owned_src;
  }

  uint8_t *rgba = malloc(count * 4);
  if (!rgba) {
    free(owned_src);
    return false;
  }
  for (size_t i = 0; i < count; i++) {
    rgba[i * 4 + 0] = 255;
    rgba[i * 4 + 1] = 255;
    rgba[i * 4 + 2] = 255;
    rgba[i * 4 + 3] = 255 - src[i];
  }

  uint32_t src_tex = R_CreateTextureRGBA(doc->canvas_w, doc->canvas_h, rgba,
                                         R_FILTER_LINEAR, R_WRAP_CLAMP);
  free(rgba);
  free(owned_src);
  if (!src_tex) return false;

  uint32_t blur_tex = 0;
  if (!bake_texture_blur((int)src_tex, doc->canvas_w, doc->canvas_h,
                         amount, &blur_tex)) {
    R_DeleteTexture(src_tex);
    return false;
  }
  R_DeleteTexture(src_tex);

  ui_render_effect_params_t p = {{0}};
  p.f[0] = expand ? 0.02f : 0.98f;
  p.f[1] = 0.08f;
  uint32_t thresh_tex = 0;
  if (!bake_texture_effect((int)blur_tex, doc->canvas_w, doc->canvas_h,
                           UI_RENDER_EFFECT_ALPHA_THRESHOLD, &p, &thresh_tex)) {
    R_DeleteTexture(blur_tex);
    return false;
  }
  R_DeleteTexture(blur_tex);

  uint8_t *out = malloc(count * 4);
  if (!out) {
    R_DeleteTexture(thresh_tex);
    return false;
  }
  bool read_ok = read_texture_rgba((int)thresh_tex, doc->canvas_w, doc->canvas_h, out);
  R_DeleteTexture(thresh_tex);
  if (!read_ok) {
    free(out);
    return false;
  }

  uint8_t *dst = malloc(count);
  if (!dst) {
    free(out);
    return false;
  }
  memset(dst, 255, count);
  bool any = false;
  for (size_t i = 0; i < count; i++) {
    uint8_t selected = MAX(out[i * 4 + 3], out[i * 4]);
    if (selected > 0) {
      dst[i] = 255 - selected;
      any = true;
    }
  }
  free(out);

  canvas_clear_selection_mask(doc);
  if (!any) {
    free(dst);
    doc->sel.active = false;
    return true;
  }

  doc->sel.mask.data = dst;
  doc->sel.mask.dirty = true;
  int x0, y0, x1, y1;
  selection_bounds(doc, &x0, &y0, &x1, &y1);
  doc->sel.start = (ipoint16_t){x0, y0};
  doc->sel.end = (ipoint16_t){x1, y1};
  doc->sel.active = true;
  return true;
}

static bool selection_modify_mask(canvas_doc_t *doc, int amount, bool expand) {
  if (amount <= 16 && selection_modify_mask_gpu(doc, amount, expand))
    return true;

  size_t count = (size_t)doc->canvas_w * doc->canvas_h;
  uint8_t *src = doc->sel.mask.data;
  uint8_t *owned_src = NULL;
  uint8_t *dst = malloc(count);
  if (!dst) return false;
  memset(dst, 255, count);

  if (!src) {
    int x0, y0, x1, y1;
    if (!selection_bounds(doc, &x0, &y0, &x1, &y1)) {
      free(dst);
      return false;
    }
    owned_src = malloc(count);
    if (!owned_src) {
      free(dst);
      return false;
    }
    memset(owned_src, 255, count);
    for (int y = y0; y <= y1; y++) {
      memset(owned_src + (size_t)y * doc->canvas_w + x0, 0,
             (size_t)(x1 - x0 + 1));
    }
    src = owned_src;
  }

  bool any = false;
  for (int y = 0; y < doc->canvas_h; y++) {
    for (int x = 0; x < doc->canvas_w; x++) {
      uint8_t v = expand ? 255 : 0;
      for (int yy = y - amount; yy <= y + amount; yy++) {
        for (int xx = x - amount; xx <= x + amount; xx++) {
          uint8_t sample = 255;
          if (xx >= 0 && xx < doc->canvas_w &&
              yy >= 0 && yy < doc->canvas_h)
            sample = src[(size_t)yy * doc->canvas_w + xx];
          v = expand ? MIN(v, sample) : MAX(v, sample);
        }
      }
      if (v < 255) {
        dst[(size_t)y * doc->canvas_w + x] = v;
        any = true;
      }
    }
  }

  free(owned_src);
  canvas_clear_selection_mask(doc);
  if (!any) {
    free(dst);
    doc->sel.active = false;
    return true;
  }
  doc->sel.mask.data = dst;
  doc->sel.mask.dirty = true;
  int x0, y0, x1, y1;
  selection_bounds(doc, &x0, &y0, &x1, &y1);
  doc->sel.start = (ipoint16_t){x0, y0};
  doc->sel.end = (ipoint16_t){x1, y1};
  doc->sel.active = true;
  return true;
}

bool canvas_expand_selection(canvas_doc_t *doc, int amount) {
  if (!doc || !doc->sel.active || amount <= 0) return false;
  if (doc->sel.move.active) canvas_commit_move(doc);
  return selection_modify_mask(doc, amount, true);
}

bool canvas_contract_selection(canvas_doc_t *doc, int amount) {
  if (!doc || !doc->sel.active || amount <= 0) return false;
  if (doc->sel.move.active) canvas_commit_move(doc);
  return selection_modify_mask(doc, amount, false);
}

// Crop the canvas to the active selection: copy the selected pixels, clear the
// entire canvas, then stamp the copied pixels at the top-left
// corner (0,0).  The selection is cleared afterwards.
void canvas_crop_to_selection(canvas_doc_t *doc) {
  if (!doc || !doc->sel.active) return;
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
  // Clear the entire canvas.
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
  doc->sel.active = false;
  canvas_clear_selection_mask(doc);
  doc->canvas_dirty = true;
}

// Crop or expand the canvas to the active selection rectangle.
// Unlike canvas_crop_to_selection(), the selection may extend outside the
// current canvas bounds — in that case the canvas grows to fit, with the new
// areas filled with transparent pixels.  If the selection is entirely inside the
// canvas the canvas shrinks (crop).  The existing pixels within the
// intersection of old and new bounds are preserved in place.
// Returns true on success, false if the state is invalid, the requested size
// exceeds the maximum, or memory allocation fails (canvas is unchanged).
bool canvas_crop_or_expand_to_selection(canvas_doc_t *doc) {
  if (!doc || !doc->sel.active) return false;
  int x0 = MIN(doc->sel.start.x, doc->sel.end.x);
  int y0 = MIN(doc->sel.start.y, doc->sel.end.y);
  int x1 = MAX(doc->sel.start.x, doc->sel.end.x);
  int y1 = MAX(doc->sel.start.y, doc->sel.end.y);
  int new_w = x1 - x0 + 1;
  int new_h = y1 - y0 + 1;

  if (new_w <= 0 || new_h <= 0) return false;
  if ((size_t)new_w > 16384 || (size_t)new_h > 16384) return false;

  for (int i = 0; i < doc->layer.count; i++) {
    if (!layer_crop_expand(doc->layer.stack[i], doc->canvas_w, doc->canvas_h,
                           x0, y0, new_w, new_h))
      return false;
  }

  free(doc->layer.composite_buf);
  doc->layer.composite_buf = malloc((size_t)new_w * new_h * 4);

  doc->canvas_w     = new_w;
  doc->canvas_h     = new_h;
  doc->pixels       = doc->layer.stack[doc->layer.active]->pixels;
  doc->canvas_dirty = true;
  doc->modified     = true;
  doc->sel.active   = false;
  canvas_clear_selection_mask(doc);
  return true;
}

// Extract the current selection into a float buffer and clear that region.
// Enters "move mode": the caller should track float_pos deltas and call
// canvas_commit_move() when the drag ends.
void canvas_begin_move(canvas_doc_t *doc, uint32_t bg) {
  if (!doc || !doc->sel.active || doc->sel.move.active) return;
  int x0, y0, x1, y1;
  if (!selection_bounds(doc, &x0, &y0, &x1, &y1)) return;
  int w = x1 - x0 + 1;
  int h = y1 - y0 + 1;
  uint8_t *buf = malloc((size_t)w * h * 4);
  uint8_t *mask = malloc((size_t)w * h);
  if (!buf || !mask) {
    free(buf);
    free(mask);
    return;
  }
  // Extract pixels
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      uint32_t c = canvas_get_pixel(doc, x0 + col, y0 + row);
      uint8_t *p = buf + ((size_t)row * w + col) * 4;
      uint8_t *m = mask + (size_t)row * w + col;
      if (canvas_in_selection(doc, x0 + col, y0 + row)) {
        p[0]=COLOR_R(c); p[1]=COLOR_G(c); p[2]=COLOR_B(c); p[3]=COLOR_A(c);
        *m = 0;
      } else {
        memset(p, 0, 4);
        *m = 255;
      }
    }
  }
  // Clear the region from canvas
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
      if (canvas_in_selection(doc, x, y))
        canvas_set_pixel_direct(doc, x, y, bg);
  doc->sel.floating.pixels  = buf;
  doc->sel.floating.mask    = mask;
  doc->sel.floating.rect    = (irect16_t){x0, y0, w, h};
  doc->sel.move.active      = true;
  canvas_clear_selection_mask(doc);
}

// Paste float_pixels back at float_pos, update selection bounds, end move.
void canvas_commit_move(canvas_doc_t *doc) {
  if (!doc || !doc->sel.move.active) return;
  int dx = doc->sel.floating.rect.x;
  int dy = doc->sel.floating.rect.y;
  int w  = doc->sel.floating.rect.w;
  int h  = doc->sel.floating.rect.h;
  size_t count = (size_t)doc->canvas_w * doc->canvas_h;
  uint8_t *new_mask = malloc(count);
  if (new_mask) memset(new_mask, 255, count);
  bool any = false;
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      size_t local_idx = (size_t)row * w + col;
      if (doc->sel.floating.mask && doc->sel.floating.mask[local_idx] != 0)
        continue;
      int x = dx + col;
      int y = dy + row;
      if (!canvas_in_bounds(doc, x, y))
        continue;
      const uint8_t *p = doc->sel.floating.pixels + local_idx * 4;
      canvas_set_pixel_direct(doc, x, y, MAKE_COLOR(p[0], p[1], p[2], p[3]));
      if (new_mask) {
        new_mask[(size_t)y * doc->canvas_w + x] = 0;
        any = true;
      }
    }
  }
  // Update selection to the new position
  canvas_clear_selection_mask(doc);
  if (new_mask && any) {
    doc->sel.mask.data = new_mask;
    doc->sel.mask.dirty = true;
    int x0, y0, x1, y1;
    selection_bounds(doc, &x0, &y0, &x1, &y1);
    doc->sel.start = (ipoint16_t){x0, y0};
    doc->sel.end = (ipoint16_t){x1, y1};
    doc->sel.active = true;
  } else {
    free(new_mask);
    doc->sel.active = false;
  }
  // Release float resources including the GL texture overlay.
  if (doc->sel.floating.tex) {
    glDeleteTextures(1, &doc->sel.floating.tex);
    doc->sel.floating.tex = 0;
  }
  free(doc->sel.floating.pixels);
  free(doc->sel.floating.mask);
  doc->sel.floating.pixels = NULL;
  doc->sel.floating.mask   = NULL;
  doc->sel.floating.rect.w      = 0;
  doc->sel.floating.rect.h      = 0;
  doc->sel.move.active   = false;
}

bool canvas_translate_selection_mask(canvas_doc_t *doc, int dx, int dy) {
  if (!doc || !doc->sel.active) return false;
  dx += doc->sel.mask.offset.x;
  dy += doc->sel.mask.offset.y;
  doc->sel.mask.offset = (ipoint16_t){0, 0};
  if (dx == 0 && dy == 0) return true;

  size_t count = (size_t)doc->canvas_w * doc->canvas_h;
  uint8_t *src = doc->sel.mask.data;
  uint8_t *owned_src = NULL;
  if (!src) {
    int x0, y0, x1, y1;
    if (!selection_bounds(doc, &x0, &y0, &x1, &y1)) return false;
    owned_src = malloc(count);
    if (!owned_src) return false;
    memset(owned_src, 255, count);
    for (int y = y0; y <= y1; y++)
      memset(owned_src + (size_t)y * doc->canvas_w + x0, 0,
             (size_t)(x1 - x0 + 1));
    src = owned_src;
  }

  uint8_t *dst = malloc(count);
  if (!dst) {
    free(owned_src);
    return false;
  }
  memset(dst, 255, count);

  bool any = false;
  for (int y = 0; y < doc->canvas_h; y++) {
    for (int x = 0; x < doc->canvas_w; x++) {
      uint8_t v = src[(size_t)y * doc->canvas_w + x];
      if (v == 255) continue;
      int nx = x + dx;
      int ny = y + dy;
      if (!canvas_in_bounds(doc, nx, ny)) continue;
      uint8_t *d = dst + (size_t)ny * doc->canvas_w + nx;
      *d = MIN(*d, v);
      any = true;
    }
  }

  free(owned_src);
  canvas_clear_selection_mask(doc);
  if (!any) {
    free(dst);
    doc->sel.active = false;
    return true;
  }

  doc->sel.mask.data = dst;
  doc->sel.mask.dirty = true;
  int x0, y0, x1, y1;
  selection_bounds(doc, &x0, &y0, &x1, &y1);
  doc->sel.start = (ipoint16_t){x0, y0};
  doc->sel.end = (ipoint16_t){x1, y1};
  doc->sel.active = true;
  return true;
}

void canvas_set_selection_mask_offset(canvas_doc_t *doc, int dx, int dy) {
  if (!doc) return;
  doc->sel.mask.offset = (ipoint16_t){dx, dy};
  if (doc->sel.active && doc->sel.mask.data) {
    int x0, y0, x1, y1;
    if (selection_bounds(doc, &x0, &y0, &x1, &y1)) {
      doc->sel.start = (ipoint16_t){x0, y0};
      doc->sel.end = (ipoint16_t){x1, y1};
    }
  }
}

bool canvas_commit_selection_mask_offset(canvas_doc_t *doc) {
  if (!doc || !doc->sel.active) return false;
  int dx = doc->sel.mask.offset.x;
  int dy = doc->sel.mask.offset.y;
  if (dx == 0 && dy == 0) return true;
  return canvas_translate_selection_mask(doc, 0, 0);
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

static inline uint8_t clamp_u8_float(float v) {
  if (v <= 0.0f) return 0;
  if (v >= 255.0f) return 255;
  return (uint8_t)(v + 0.5f);
}

static void sample_layer_nearest(const layer_t *lay, int old_w, int old_h,
                                 float sx, float sy, uint8_t out[4]) {
  int ix = (int)floorf(sx + 0.5f);
  int iy = (int)floorf(sy + 0.5f);
  ix = MAX(0, MIN(old_w - 1, ix));
  iy = MAX(0, MIN(old_h - 1, iy));
  const uint8_t *p = lay->pixels + ((size_t)iy * old_w + ix) * 4;
  memcpy(out, p, 4);
}

static void sample_layer_bilinear(const layer_t *lay, int old_w, int old_h,
                                  float sx, float sy, uint8_t out[4]) {
  int x0 = (int)floorf(sx);
  int y0 = (int)floorf(sy);
  float tx = sx - (float)x0;
  float ty = sy - (float)y0;
  int x1 = x0 + 1;
  int y1 = y0 + 1;
  x0 = MAX(0, MIN(old_w - 1, x0));
  y0 = MAX(0, MIN(old_h - 1, y0));
  x1 = MAX(0, MIN(old_w - 1, x1));
  y1 = MAX(0, MIN(old_h - 1, y1));

  float acc_r = 0.0f, acc_g = 0.0f, acc_b = 0.0f, acc_a = 0.0f;
  const int xs[2] = { x0, x1 };
  const int ys[2] = { y0, y1 };
  const float wx[2] = { 1.0f - tx, tx };
  const float wy[2] = { 1.0f - ty, ty };
  for (int yy = 0; yy < 2; yy++) {
    for (int xx = 0; xx < 2; xx++) {
      float w = wx[xx] * wy[yy];
      const uint8_t *p = lay->pixels + ((size_t)ys[yy] * old_w + xs[xx]) * 4;
      float a = (float)p[3];
      acc_r += (float)p[0] * a * w;
      acc_g += (float)p[1] * a * w;
      acc_b += (float)p[2] * a * w;
      acc_a += a * w;
    }
  }

  if (acc_a > 0.0f) {
    out[0] = clamp_u8_float(acc_r / acc_a);
    out[1] = clamp_u8_float(acc_g / acc_a);
    out[2] = clamp_u8_float(acc_b / acc_a);
  } else {
    out[0] = out[1] = out[2] = 0;
  }
  out[3] = clamp_u8_float(acc_a);
}

static uint8_t *layer_resample_pixels(const layer_t *lay, int old_w, int old_h,
                                      int new_w, int new_h,
                                      image_resize_filter_t filter) {
  uint8_t *buf = malloc((size_t)new_w * new_h * 4);
  if (!buf) return NULL;
  float sx_scale = (float)old_w / (float)new_w;
  float sy_scale = (float)old_h / (float)new_h;
  for (int y = 0; y < new_h; y++) {
    for (int x = 0; x < new_w; x++) {
      float sx = ((float)x + 0.5f) * sx_scale - 0.5f;
      float sy = ((float)y + 0.5f) * sy_scale - 0.5f;
      uint8_t *dst = buf + ((size_t)y * new_w + x) * 4;
      if (filter == IMAGE_RESIZE_NEAREST)
        sample_layer_nearest(lay, old_w, old_h, sx, sy, dst);
      else
        sample_layer_bilinear(lay, old_w, old_h, sx, sy, dst);
    }
  }
  return buf;
}

static void layer_replace_pixels(layer_t *lay, uint8_t *pixels) {
  if (!lay || !pixels) return;
  free(lay->pixels);
  lay->pixels = pixels;
  if (lay->tex) {
    glDeleteTextures(1, &lay->tex);
    lay->tex = 0;
  }
  lay->preview.active = false;
}

// Resize the image contents to new_w x new_h.
// All layer pixel buffers are resampled with the requested filter.
bool canvas_resize_image(canvas_doc_t *doc, int new_w, int new_h,
                         image_resize_filter_t filter) {
  if (!doc || new_w <= 0 || new_h <= 0) return false;
  if (new_w == doc->canvas_w && new_h == doc->canvas_h) return true;
  if ((size_t)new_w > 16384 || (size_t)new_h > 16384) return false;
  if (filter < 0 || filter >= IMAGE_RESIZE_FILTER_COUNT)
    filter = IMAGE_RESIZE_BILINEAR;

  uint8_t **new_pixels = calloc((size_t)doc->layer.count, sizeof(uint8_t *));
  if (!new_pixels) return false;
  for (int i = 0; i < doc->layer.count; i++) {
    new_pixels[i] = layer_resample_pixels(doc->layer.stack[i],
                                          doc->canvas_w, doc->canvas_h,
                                          new_w, new_h, filter);
    if (!new_pixels[i]) {
      for (int j = 0; j < i; j++) free(new_pixels[j]);
      free(new_pixels);
      return false;
    }
  }

  for (int i = 0; i < doc->layer.count; i++)
    layer_replace_pixels(doc->layer.stack[i], new_pixels[i]);
  free(new_pixels);

  free(doc->layer.composite_buf);
  doc->layer.composite_buf = malloc((size_t)new_w * new_h * 4);
  doc->canvas_w     = new_w;
  doc->canvas_h     = new_h;
  doc->pixels       = doc->layer.stack[doc->layer.active]->pixels;
  doc->canvas_dirty = true;
  doc->modified     = true;
  doc->sel.active   = false;
  canvas_clear_selection_mask(doc);
  return true;
}

// Resize the canvas to new_w x new_h.
// All layer pixel buffers are resized. Existing pixels are preserved
// at the top-left corner; any new area is filled with transparent pixels.
// The GL texture is invalidated so it will be re-created on the next paint.
// Returns true on success, false if an allocation fails (canvas may be partially
// resized in that case; callers should treat it as a fatal document error).
bool canvas_resize(canvas_doc_t *doc, int new_w, int new_h) {
  if (!doc || new_w <= 0 || new_h <= 0) return false;
  if (new_w == doc->canvas_w && new_h == doc->canvas_h) return true;
  if ((size_t)new_w > 16384 || (size_t)new_h > 16384) return false;

  for (int i = 0; i < doc->layer.count; i++) {
    if (!layer_crop_expand(doc->layer.stack[i], doc->canvas_w, doc->canvas_h,
                           0, 0, new_w, new_h))
      return false;
  }

  free(doc->layer.composite_buf);
  doc->layer.composite_buf = malloc((size_t)new_w * new_h * 4);
  // If this allocation fails, canvas_upload will retry and skip rendering
  // until it succeeds.  The document's layer data is already valid.

  doc->canvas_w     = new_w;
  doc->canvas_h     = new_h;
  doc->pixels       = doc->layer.stack[doc->layer.active]->pixels;
  doc->canvas_dirty = true;
  doc->modified     = true;
  doc->sel.active   = false;
  canvas_clear_selection_mask(doc);
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
  canvas_composite_over_bg(doc, comp);
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
  for (int i = 0; i < doc->layer.count && !need_upload; i++) {
    if (!doc->layer.stack[i]->tex)
      need_upload = true;
  }

  if (need_upload) {
    for (int i = 0; i < doc->layer.count; i++)
      layer_upload_texture(doc, doc->layer.stack[i]);
    doc->canvas_dirty = false;
  }
}
