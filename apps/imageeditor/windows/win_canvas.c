// Canvas child window – renders the pixel canvas and dispatches paint events to tools

#include "imageeditor.h"

// Single source of truth for zoom levels and their View menu IDs.
// win_menubar.c also references these via the extern declarations in imageeditor.h.
const int kZoomLevels[NUM_ZOOM_LEVELS]  = {1, 2, 4, 6, 8};
const int kZoomMenuIDs[NUM_ZOOM_LEVELS] = {
  ID_VIEW_ZOOM_1X, ID_VIEW_ZOOM_2X, ID_VIEW_ZOOM_4X,
  ID_VIEW_ZOOM_6X, ID_VIEW_ZOOM_8X
};

// Round v to the nearest multiple of step (snap-to-grid helper).
#define SNAP_AXIS(v, step) (((v) + (step) / 2) / (step) * (step))

static int scaled_px(int px, float scale) {
  return (int)lroundf((float)px * scale);
}

static int canvas_view_w(int win_w) {
  return MAX(0, win_w);
}

static int canvas_scaled_w(const canvas_doc_t *doc, float scale) {
  return doc ? (scaled_px(doc->canvas_w, scale) / g_bw_retina_scale) : 0;
}

static int canvas_scaled_h(const canvas_doc_t *doc, float scale) {
  return doc ? (scaled_px(doc->canvas_h, scale) / g_bw_retina_scale) : 0;
}

float imageeditor_fit_scale_for_viewport(int content_w, int content_h,
                                         int viewport_w, int viewport_h,
                                         bool allow_zoom_in) {
  if (content_w <= 0 || content_h <= 0 || viewport_w <= 0 || viewport_h <= 0)
    return 1.0f;

  float raw = fminf((float)viewport_w / (float)content_w,
                    (float)viewport_h / (float)content_h);
  if (raw <= 0.0f) return 1.0f;

  if (!allow_zoom_in && raw >= 1.0f)
    return 1.0f;

  if (raw >= 1.0f) {
    float fit = 1.0f;
    for (int i = 0; i < NUM_ZOOM_LEVELS; i++) {
      if ((float)kZoomLevels[i] <= raw)
        fit = (float)kZoomLevels[i];
    }
    return fit;
  }

  return raw;
}

void imageeditor_format_zoom(char *buf, size_t buf_sz, float scale) {
  if (!buf || buf_sz == 0) return;
  if (fabsf(scale - roundf(scale)) < 0.01f) {
    snprintf(buf, buf_sz, "%dx", (int)lroundf(scale));
  } else {
    snprintf(buf, buf_sz, "%.2fx", scale);
  }
}

bool imageeditor_handle_zoom_command(canvas_doc_t *doc, uint32_t id) {
  if (!doc || !doc->canvas_win) return false;
  canvas_win_state_t *state = (canvas_win_state_t *)doc->canvas_win->userdata;
  if (!state) return false;

  int new_scale = -1;

  if (id == ID_VIEW_ZOOM_FIT) {
    canvas_win_fit_zoom(doc->canvas_win);
  } else if (id == ID_VIEW_ZOOM_IN) {
    for (int i = 0; i < NUM_ZOOM_LEVELS; i++) {
      if (kZoomLevels[i] > window_view_zoom(doc->canvas_win)) { new_scale = kZoomLevels[i]; break; }
    }
  } else if (id == ID_VIEW_ZOOM_OUT) {
    for (int i = NUM_ZOOM_LEVELS - 1; i >= 0; i--) {
      if (kZoomLevels[i] < window_view_zoom(doc->canvas_win)) { new_scale = kZoomLevels[i]; break; }
    }
  } else {
    for (int i = 0; i < NUM_ZOOM_LEVELS; i++) {
      if (kZoomMenuIDs[i] == (int)id) { new_scale = kZoomLevels[i]; break; }
    }
  }

  if (id != ID_VIEW_ZOOM_FIT && new_scale < 0) return false;
  if (new_scale >= 0)
    canvas_win_set_zoom(doc->canvas_win, new_scale);

  char zoom_msg[32];
  char zoom_text[16];
  imageeditor_format_zoom(zoom_text, sizeof(zoom_text), window_view_zoom(doc->canvas_win));
  snprintf(zoom_msg, sizeof(zoom_msg), "Zoom: %s", zoom_text);
  send_message(doc->win, evStatusBar, 0, zoom_msg);
  return true;
}

void canvas_win_update_status(window_t *win, int px, int py, bool hover_valid) {
  if (!win) return;
  canvas_win_state_t *state = (canvas_win_state_t *)win->userdata;
  canvas_doc_t *doc = state ? state->doc : NULL;
  if (!state || !doc || !doc->win) return;

  int log_w = doc->canvas_w / g_bw_retina_scale;
  int log_h = doc->canvas_h / g_bw_retina_scale;
  char sb[64];
  const char *view_suffix = doc->layer.mask_only_view ? "  [Mask Only]" : "";
  const char *edit_suffix = (doc->layer.editing_mask && doc->layer.count > 0) ? "  [Mask]" : "";
  if (hover_valid && g_app && g_app->current_tool == ID_TOOL_SELECT &&
      doc->drawing && doc->sel.active && !doc->sel.move.active) {
    int sel_w = abs(px - doc->sel.start.x) + 1;
    int sel_h = abs(py - doc->sel.start.y) + 1;
    snprintf(sb, sizeof(sb), "Selection: %dx%d  |  %dx%d%s%s",
             sel_w, sel_h, log_w, log_h,
             view_suffix, edit_suffix);
  } else if (hover_valid) {
    snprintf(sb, sizeof(sb), "x=%d, y=%d  |  %dx%d%s%s",
             px, py, log_w, log_h,
             view_suffix, edit_suffix);
  } else {
    snprintf(sb, sizeof(sb), "%dx%d%s%s",
             log_w, log_h, view_suffix, edit_suffix);
  }

  if (strcmp(sb, state->last_sb) != 0) {
    strncpy(state->last_sb, sb, sizeof(state->last_sb) - 1);
    state->last_sb[sizeof(state->last_sb) - 1] = '\0';
    send_message(doc->win, evStatusBar, 0, sb);
  }
}

// Return the logical brush radius for the current brush_size, clamping any
// out-of-range value to the nearest valid index to prevent OOB reads.
static int brush_radius(void) {
  int idx = g_app ? g_app->brush_size : 0;
  if (idx < 0) idx = 0;
  if (idx >= NUM_BRUSH_SIZES) idx = NUM_BRUSH_SIZES - 1;
  return kBrushSizes[idx];
}

// Apply snap-to-grid to a canvas pixel position if the grid snap option is
// enabled.  Rounds px/py to the nearest grid intersection.
static void snap_canvas_pos(int *px, int *py) {
  if (!g_app || !g_app->grid.snap) return;
  int gx = g_app->grid.spacing.x;
  int gy = g_app->grid.spacing.y;
  if (gx > 1) *px = SNAP_AXIS(*px, gx);
  if (gy > 1) *py = SNAP_AXIS(*py, gy);
}

// The document owns the scrollbars; the child owns its content transform.
static void canvas_sync_scrollbars(window_t *win, canvas_win_state_t *state) {
  canvas_doc_t *doc = state->doc;
  window_view_set_size(win, doc->canvas_w, doc->canvas_h);
#ifndef AX_PLATFORM_IOS
  window_t *owner = doc->win;
  if (!owner) return;
  frect_t bounds = window_view_bounds(win);
  set_scroll_content(owner, (int)ceilf(bounds.w), (int)ceilf(bounds.h),
                     window_view_scroll(win, SB_HORZ), window_view_scroll(win, SB_VERT));
  if (!win->view.free_pan) {
    window_view_set_scroll(win, SB_HORZ, get_scroll_pos(owner, SB_HORZ));
    window_view_set_scroll(win, SB_VERT, get_scroll_pos(owner, SB_VERT));
  }
#endif
}

// Set a continuous zoom level through the window's content view.
void canvas_win_set_scale(window_t *win, float new_scale) {
  canvas_win_state_t *state = (canvas_win_state_t *)win->userdata;
  if (!state) return;
  window_view_set_zoom(win, MAX(0.05f, new_scale), NULL);
  canvas_sync_scrollbars(win, state);
  invalidate_window(win);
}

// Set zoom level on a canvas window (called by menu/accelerator handler).
// new_scale is snapped to the nearest supported zoom level so callers can
// never trigger a divide-by-zero or produce unexpected canvas sizes.
void canvas_win_set_zoom(window_t *win, int new_scale) {
  canvas_win_state_t *state = (canvas_win_state_t *)win->userdata;
  if (!state) return;

  // Snap new_scale to the closest supported zoom level
  int clamped = kZoomLevels[0];
  if (new_scale <= kZoomLevels[0]) {
    clamped = kZoomLevels[0];
  } else if (new_scale >= kZoomLevels[NUM_ZOOM_LEVELS - 1]) {
    clamped = kZoomLevels[NUM_ZOOM_LEVELS - 1];
  } else {
    for (int i = 1; i < NUM_ZOOM_LEVELS; i++) {
      if (new_scale <= kZoomLevels[i]) {
        int dist_prev = new_scale - kZoomLevels[i - 1];
        int dist_curr = kZoomLevels[i] - new_scale;
        clamped = (dist_prev <= dist_curr) ? kZoomLevels[i - 1] : kZoomLevels[i];
        break;
      }
    }
  }

  canvas_win_set_scale(win, (float)clamped);
}

// Fit the canvas to the viewport at the largest integer zoom where the entire
// image is visible — equivalent to Photoshop's "Fit on Screen" (Ctrl+0).
// Falls back to 1x when no zoom level fits (image larger than viewport).
// Centers the canvas in the viewport after zooming.
void canvas_win_fit_zoom(window_t *win) {
  canvas_win_state_t *state = (canvas_win_state_t *)win->userdata;
  if (!state || !state->doc) return;
  canvas_doc_t *doc = state->doc;

  int view_w = canvas_view_w(win->frame.w);
  int view_h = win->frame.h;
  if (view_w <= 0 || view_h <= 0) return;

  float fit_scale = imageeditor_fit_scale_for_viewport(canvas_scaled_w(doc, 1), canvas_scaled_h(doc, 1),
                                                       view_w, view_h, true);
  if (fit_scale < 1.0f) fit_scale = 1.0f;

  canvas_win_set_scale(win, fit_scale);
  window_view_center(win);
  canvas_sync_scrollbars(win, state);
}

// Public helper: re-clamp pan and update scrollbars without changing zoom.
// Call this after canvas_resize() to keep scrollbar state consistent.
void canvas_win_sync_scrollbars(window_t *win) {
  canvas_win_state_t *state = (canvas_win_state_t *)win->userdata;
  if (!state) return;
  canvas_sync_scrollbars(win, state);
}

// Release the floating-selection GL texture if one exists.
static void float_tex_free(canvas_doc_t *doc) {
  if (doc->sel.floating.tex) {
    glDeleteTextures(1, &doc->sel.floating.tex);
    doc->sel.floating.tex = 0;
  }
}

// Upload float_pixels into a (re)created float_tex.
static void float_tex_upload(canvas_doc_t *doc) {
  float_tex_free(doc);
  if (!g_ui_runtime.running) return;
  if (!doc->sel.floating.pixels || doc->sel.floating.rect.w <= 0 || doc->sel.floating.rect.h <= 0) return;
  glGenTextures(1, &doc->sel.floating.tex);
  glBindTexture(GL_TEXTURE_2D, doc->sel.floating.tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
               doc->sel.floating.rect.w, doc->sel.floating.rect.h, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, doc->sel.floating.pixels);
}

static bool selection_move_hit(const canvas_doc_t *doc, int x, int y) {
  if (!doc || !doc->sel.active || !canvas_in_bounds(doc, x, y)) return false;
  if (doc->sel.mask.data) {
    int sx = x - doc->sel.mask.offset.x;
    int sy = y - doc->sel.mask.offset.y;
    if (!canvas_in_bounds(doc, sx, sy)) return false;
    return doc->sel.mask.data[(size_t)sy * doc->canvas_w + sx] < 128;
  }
  return canvas_in_selection(doc, x, y);
}

// Keep the clicked image pixel under the pointer.
static void apply_zoom_centered(window_t *win, canvas_win_state_t *state, int scale, ipoint16_t anchor) {
  window_view_set_zoom(win, scale, &anchor);
  canvas_sync_scrollbars(win, state);
}

static void canvas_draw_grid(window_t *win) {
  if (!g_app || !g_app->grid.visible) return;
  irect16_t visible = window_view_visible_rect(win);
  int gx = MAX(1, g_app->grid.spacing.x), gy = MAX(1, g_app->grid.spacing.y);
  for (int y = gy; y < visible.y + visible.h; y += gy)
    if (y >= visible.y) draw_sel_rect(R(visible.x, y, visible.w, 1));
  for (int x = gx; x < visible.x + visible.w; x += gx)
    if (x >= visible.x) draw_sel_rect(R(x, visible.y, 1, visible.h));
}

static void canvas_draw_selection_mask_overlay(canvas_doc_t *doc,
                                               canvas_win_state_t *state,
                                               window_t *win) {
  if (!doc || !state || !doc->sel.active || !doc->sel.mask.data || !g_app) return;
  if (!g_ui_runtime.running) return;

  if (!doc->sel.mask.tex) {
    glGenTextures(1, &doc->sel.mask.tex);
    glBindTexture(GL_TEXTURE_2D, doc->sel.mask.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLint swizzle[] = { GL_ONE, GL_ONE, GL_ONE, GL_RED };
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, swizzle[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, swizzle[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, swizzle[2]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, swizzle[3]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, doc->canvas_w, doc->canvas_h, 0,
                 GL_RED, GL_UNSIGNED_BYTE, doc->sel.mask.data);
    doc->sel.mask.dirty = false;
  } else if (doc->sel.mask.dirty) {
    glBindTexture(GL_TEXTURE_2D, doc->sel.mask.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, doc->canvas_w, doc->canvas_h,
                    GL_RED, GL_UNSIGNED_BYTE, doc->sel.mask.data);
    doc->sel.mask.dirty = false;
  }

  irect16_t canvas_rect = R(0, 0, doc->canvas_w, doc->canvas_h);
  ui_render_effect_params_t params = {{0}};
  params.f[0] = (float)doc->sel.mask.offset.x / (float)doc->canvas_w;
  params.f[1] = (float)doc->sel.mask.offset.y / (float)doc->canvas_h;
  params.f[4] = (float)COLOR_R(g_app->wand.overlay_color) / 255.0f;
  params.f[5] = (float)COLOR_G(g_app->wand.overlay_color) / 255.0f;
  params.f[6] = (float)COLOR_B(g_app->wand.overlay_color) / 255.0f;
  params.f[7] = (float)COLOR_A(g_app->wand.overlay_color) / 255.0f;
  imageeditor_draw_rect_effect((int)doc->sel.mask.tex,
                               canvas_rect.x, canvas_rect.y, canvas_rect.w, canvas_rect.h,
                               IE_RENDER_EFFECT_SELECTION_MASK, &params);
}

static void canvas_draw_animation_trace(window_t *win,
                                        canvas_win_state_t *state,
                                        canvas_doc_t *doc,
                                        irect16_t canvas_rect) {
  if (!win || !state || !doc || !doc->anim || !g_app || !g_app->anim_trace_enabled)
    return;
  if (doc->layer.mask_only_view || doc->anim->playing || doc->anim->frame_count <= 1)
    return;

  if (state->onion_tex_w != doc->canvas_w || state->onion_tex_h != doc->canvas_h) {
    if (state->onion_tex) {
      glDeleteTextures(1, &state->onion_tex);
      state->onion_tex = 0;
    }
    state->onion_tex_w = doc->canvas_w;
    state->onion_tex_h = doc->canvas_h;
  }

  int prev_steps = 0;
  int next_steps = 0;
  for (int i = 0; i < ONION_SKIN_MAX_STEPS; i++) {
    if (g_app->anim_trace_prev_opacity[i] > 0) prev_steps = i + 1;
    if (g_app->anim_trace_next_opacity[i] > 0) next_steps = i + 1;
  }

  for (int step = prev_steps; step >= 1; step--) {
    int idx = doc->anim->active_frame - step;
    if (idx < 0) continue;
    float alpha = (float)g_app->anim_trace_prev_opacity[step - 1] / 100.0f;
    if (alpha <= 0.0f) continue;
    const anim_frame_t *frame = doc->anim->frames[idx];
    if (!frame) continue;
    if (anim_render_frame_thumbnail(frame, doc->canvas_w, doc->canvas_h, &state->onion_tex,
#if IMAGEEDITOR_INDEXED
                                    doc->ipal.entries
#else
                                    NULL
#endif
                                    ))
      draw_rect_ex((int)state->onion_tex, canvas_rect, 0, CLAMP(alpha, 0.0f, 1.0f));
  }

  for (int step = next_steps; step >= 1; step--) {
    int idx = doc->anim->active_frame + step;
    if (idx >= doc->anim->frame_count) continue;
    float alpha = (float)g_app->anim_trace_next_opacity[step - 1] / 100.0f;
    if (alpha <= 0.0f) continue;
    const anim_frame_t *frame = doc->anim->frames[idx];
    if (!frame) continue;
    if (anim_render_frame_thumbnail(frame, doc->canvas_w, doc->canvas_h, &state->onion_tex,
#if IMAGEEDITOR_INDEXED
                                    doc->ipal.entries
#else
                                    NULL
#endif
                                    ))
      draw_rect_ex((int)state->onion_tex, canvas_rect, 0, CLAMP(alpha, 0.0f, 1.0f));
  }
}

static void canvas_draw_loupe(window_t *win, canvas_win_state_t *state, canvas_doc_t *doc) {
  // Magnifier tool: draw a loupe overlay in the top-right corner of the canvas
  // showing a 16x16 canvas-pixel region centered on the cursor at 4x zoom.
  // Rendered as a single textured quad to avoid 256 fill_rect() calls.
  enum { MAG_PIXELS = 16, MAG_ZOOM = 4, MAG_SIZE = MAG_PIXELS * MAG_ZOOM, MAG_MARGIN = 4 };
  if (g_app && g_app->current_tool == ID_TOOL_MAGNIFIER &&
      state->hover_valid &&
      win->frame.w  >= MAG_SIZE + MAG_MARGIN * 2 + 4 &&
      win->frame.h  >= MAG_SIZE + MAG_MARGIN * 2 + 4) {
    int lox = win->frame.w - MAG_SIZE - MAG_MARGIN - 2;
    int loy = MAG_MARGIN;
    // Border
    fill_rect(0xFF808080, R(lox - 1, loy - 1, MAG_SIZE + 2, MAG_SIZE + 2));
    // Build a 16x16 RGBA pixel buffer from the canvas region around hover
    uint8_t mag_buf[MAG_PIXELS * MAG_PIXELS * 4];
    int hx = state->hover.x - MAG_PIXELS / 2;
    int hy = state->hover.y - MAG_PIXELS / 2;
    for (int row = 0; row < MAG_PIXELS; row++) {
      for (int col = 0; col < MAG_PIXELS; col++) {
        int sx = hx + col, sy = hy + row;
        uint32_t px = canvas_in_bounds(doc, sx, sy)
                    ? canvas_get_pixel(doc, sx, sy)
                    : MAKE_COLOR(0x22, 0x22, 0x22, 0xFF);
        uint8_t *dst = mag_buf + (row * MAG_PIXELS + col) * 4;
        dst[0] = COLOR_R(px); dst[1] = COLOR_G(px); dst[2] = COLOR_B(px); dst[3] = COLOR_A(px);
      }
    }
    // Upload pixel buffer to a cached GL texture and draw as a single quad
    if (!state->mag_tex) {
      glGenTextures(1, &state->mag_tex);
      glBindTexture(GL_TEXTURE_2D, state->mag_tex);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, MAG_PIXELS, MAG_PIXELS, 0,
                   GL_RGBA, GL_UNSIGNED_BYTE, mag_buf);
    } else {
      glBindTexture(GL_TEXTURE_2D, state->mag_tex);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, MAG_PIXELS, MAG_PIXELS,
                      GL_RGBA, GL_UNSIGNED_BYTE, mag_buf);
    }
    draw_rect(state->mag_tex, R(lox, loy, MAG_SIZE, MAG_SIZE));
    // Crosshair at loupe center
    int lcx = lox + MAG_SIZE / 2;
    int lcy = loy + MAG_SIZE / 2;
    fill_rect(0xFF000000, R(lcx - 3, lcy, 3, 1));
    fill_rect(0xFF000000, R(lcx + 1, lcy, 3, 1));
    fill_rect(0xFF000000, R(lcx, lcy - 3, 1, 3));
    fill_rect(0xFF000000, R(lcx, lcy + 1, 1, 3));
  }
}

result_t win_canvas_proc(window_t *win, uint32_t msg,
                          uint32_t wparam, void *lparam) {
  canvas_win_state_t *state = (canvas_win_state_t *)win->userdata;
  canvas_doc_t *doc = state ? state->doc : NULL;
  switch (msg) {
    case evCreate: {
      canvas_win_state_t *s = allocate_window_data(win, sizeof(canvas_win_state_t));
      s->doc = (canvas_doc_t *)lparam;
      s->doc->canvas_win = win;
      window_view_init(win, s->doc->canvas_w, s->doc->canvas_h, g_bw_retina_scale, true);
      // Sync the document window's built-in scrollbars.
      canvas_sync_scrollbars(win, s);
      return true;
    }

    case evDestroy: {
      if (state && state->mag_tex) {
        glDeleteTextures(1, &state->mag_tex);
        state->mag_tex = 0;
      }
      if (state && state->onion_tex) {
        glDeleteTextures(1, &state->onion_tex);
        state->onion_tex = 0;
      }
      return false;
    }

    case evSetFocus:
      if (g_app && doc) {
        g_app->active_doc = doc;
        imageeditor_sync_main_toolbar();
      }
      return false;

    case evPaint: {
      if (!state || !doc) return true;
      if (wparam == WINDOW_PAINT_OVERLAY) {
        canvas_draw_loupe(win, state, doc);
        return true;
      }
      canvas_upload(doc);
      irect16_t canvas_rect = R(0, 0, doc->canvas_w, doc->canvas_h);
      if (!doc->layer.mask_only_view) {
        if (doc->background.show)
          fill_rect(doc->background.color, canvas_rect);
        else
          draw_checkerboard(canvas_rect, CANVAS_CHECKER_SQUARE_PX);
        canvas_draw_animation_trace(win, state, doc, canvas_rect);
        for (int li = 0; li < doc->layer.count; li++) {
          const layer_t *lay = doc->layer.stack[li];
          if (!lay || !lay->visible) continue;
          if (!lay->tex) continue;
          if (lay->preview.active) {
            imageeditor_draw_rect_effect_blend(lay->tex,
                                               canvas_rect.x, canvas_rect.y,
                                               canvas_rect.w, canvas_rect.h,
                                               lay->opacity / 255.0f,
                                               (ui_layer_blend_t)lay->blend_mode,
                                               lay->preview.effect,
                                               &lay->preview.params);
          } else {
            draw_rect_blend(lay->tex,
                            canvas_rect.x, canvas_rect.y,
                            canvas_rect.w, canvas_rect.h,
                            lay->opacity / 255.0f,
                            (ui_layer_blend_t)lay->blend_mode);
          }
        }
      } else if (doc->layer.active >= 0 && doc->layer.active < doc->layer.count) {
        const layer_t *lay = doc->layer.stack[doc->layer.active];
        if (lay && lay->tex) {
          if (lay->preview.active) {
            imageeditor_draw_rect_effect_blend(lay->tex,
                                               canvas_rect.x, canvas_rect.y,
                                               canvas_rect.w, canvas_rect.h,
                                               lay->opacity / 255.0f,
                                               UI_LAYER_BLEND_NORMAL,
                                               lay->preview.effect,
                                               &lay->preview.params);
          } else {
            imageeditor_draw_rect_effect(lay->tex,
                                         canvas_rect.x, canvas_rect.y,
                                         canvas_rect.w, canvas_rect.h,
                                         IE_RENDER_EFFECT_MASK_GRAYSCALE, NULL);
          }
        }
      }

      // Draw grid overlay (same checker-texture mechanism as selection)
      canvas_draw_grid(win);

      canvas_draw_selection_mask_overlay(doc, state, win);

      if (doc->sel.move.active && doc->sel.floating.tex) {
        // Draw the floating selection at its current position
        irect16_t float_rect = doc->sel.floating.rect;
        draw_rect(doc->sel.floating.tex, float_rect);
        draw_sel_rect(float_rect);
      } else if (doc->sel.active &&
                 (IMAGEEDITOR_SHOW_SELECTION_BOUNDS ||
                  (g_app && ((g_app->current_tool == ID_TOOL_SELECT && doc->drawing) ||
                             g_app->current_tool == ID_TOOL_CROP)))) {
        irect16_t sel_rect = R(MIN(doc->sel.start.x, doc->sel.end.x), MIN(doc->sel.start.y, doc->sel.end.y),
                               abs(doc->sel.end.x - doc->sel.start.x) + 1, abs(doc->sel.end.y - doc->sel.start.y) + 1);
        draw_sel_rect(sel_rect);
      }
      // Polygon in-progress: draw a sel_rect bounding the rubber-band edge
      // from the last committed vertex to the current mouse position.
      if (doc->poly.active && doc->poly.count > 0) {
        ipoint16_t v0 = doc->poly.pts[doc->poly.count - 1];
        ipoint16_t v1 = doc->last;
        irect16_t poly_rect = R(MIN(v0.x, v1.x), MIN(v0.y, v1.y), abs(v1.x - v0.x) + 1, abs(v1.y - v0.y) + 1);
        draw_sel_rect(poly_rect);
      }

      return true;
    }

    case evHScroll:
      if (state) {
        IE_TRACE("hscroll win=%u pos=%u", win->id, wparam);
        window_view_set_scroll(win, SB_HORZ, (int)wparam);
        canvas_sync_scrollbars(win, state);
        invalidate_window(win);
      }
      return true;

    case evVScroll:
      if (state) {
        IE_TRACE("vscroll win=%u pos=%u", win->id, wparam);
        window_view_set_scroll(win, SB_VERT, (int)wparam);
        canvas_sync_scrollbars(win, state);
        invalidate_window(win);
      }
      return true;

    case evCommand: {
      if (!state) return false;
      return false;
    }

    case evWheel: {
      if (!state) return false;
      // Never scroll while a drawing stroke is in progress – changing pan
      // mid-stroke would invalidate doc->last (stored in pre-pan pixel coords)
      // and produce a visible position jump on the next MouseMove segment.
      if (doc && doc->drawing) return true;
      window_view_pan(win, (ipoint16_t){(int16_t)LOWORD((uintptr_t)lparam), (int16_t)HIWORD((uintptr_t)lparam)});
      canvas_sync_scrollbars(win, state);
      return true;
    }

    case evGesture: {
      if (!state || !doc || !lparam) return false;
      const ax_gesture_t *gesture = lparam;
      if (gesture->phase == AX_GESTURE_BEGIN) {
        state->gesture_active = true;
        state->hover_valid = false;
        set_focus(win);
      } else if (gesture->phase == AX_GESTURE_UPDATE && state->gesture_active) {
        window_view_apply_gesture(win, gesture);
        invalidate_window(win);
      } else {
        state->gesture_active = false;
      }
      IE_TRACE("gesture win=%u phase=%u zoom=%.3f", win->id, gesture->phase, window_view_zoom(win));
      return true;
    }
    case evPointerCancel: {
      if (!state || !doc || !g_app) return false;
      state->pan.active = false;
      canvas_stroke_cancel(doc);
      if (doc->drawing) {
        if (canvas_is_shape_tool(g_app->current_tool)) {
          if (doc->shape.snapshot) {
            memcpy(doc->pixels, doc->shape.snapshot, (size_t)doc->canvas_w * doc->canvas_h * DOC_BPP);
            free(doc->shape.snapshot); doc->shape.snapshot = NULL;
          }
        } else if (state->stroke_undo) {
          if (!doc_cancel_undo(doc)) IE_TRACE("pointer cancel restore failed win=%u", win->id);
        }
        doc->drawing = false;
        doc->modified = state->stroke_modified;
        doc->canvas_dirty = true;
        if (doc->sel.move.active) canvas_discard_move(doc);
        if (doc->sel.move.mask_moving) canvas_set_selection_mask_offset(doc, 0, 0);
        doc->sel.move.mask_moving = false;
        if (g_app->current_tool == ID_TOOL_CROP || g_app->current_tool == ID_TOOL_SELECT)
          canvas_deselect(doc);
        doc_update_title(doc);
      }
      IE_TRACE("pointer cancel win=%u", win->id);
      invalidate_window(win);
      return true;
    }
    case evLeftButtonDown: {
      if (!state) return true;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);

      if (!doc || !g_app) return true;
      if (state->gesture_active) return true;
      canvas_stroke_cancel(doc);
      state->stroke_modified = doc->modified;
      state->stroke_undo = false;
      ipoint16_t doc_pt = {lx, ly};
      // Clear any stale panning state – if the user switched away from Hand
      // while holding the button, panning must not bleed into MouseMove.
      if (g_app->current_tool != ID_TOOL_HAND) state->pan.active = false;

      // Hand tool: begin pan drag
      if (g_app->current_tool == ID_TOOL_HAND) {
        state->pan.active = true;
        window_view_begin_drag(win);
        IE_DEBUG("pan_begin doc=%p at=(%d,%d)", (void *)doc, lx, ly);
        return true;
      }

      // Zoom tool (left click): zoom in centered on cursor
      if (g_app->current_tool == ID_TOOL_ZOOM) {
        int new_scale = -1;
        for (int i = 0; i < NUM_ZOOM_LEVELS; i++) {
          if (kZoomLevels[i] > window_view_zoom(win)) { new_scale = kZoomLevels[i]; break; }
        }
        if (new_scale > 0)
          apply_zoom_centered(win, state, new_scale, doc_pt);
        return true;
      }

      // Eyedropper (left click): pick foreground color from canvas pixel
      if (g_app->current_tool == ID_TOOL_EYEDROPPER) {
        int px = doc_pt.x;
        int py = doc_pt.y;
        if (canvas_in_bounds(doc, px, py)) {
          g_app->fg_color = canvas_get_pixel(doc, px, py);
#if IMAGEEDITOR_INDEXED
          g_app->fg_palette_idx = doc->pixels[(size_t)py * doc->canvas_w + px];
#endif
          if (g_app->tool_win)  invalidate_window(g_app->tool_win);
          if (g_app->color_win) invalidate_window(g_app->color_win);
        }
        return true;
      }

      // Magnifier tool: the loupe is a passive overlay; clicks have no effect
      if (g_app->current_tool == ID_TOOL_MAGNIFIER) return true;

      int px = doc_pt.x;
      int py = doc_pt.y;
      snap_canvas_pos(&px, &py);
      int tool = g_app->current_tool;
      bool shift = (ui_get_mod_state() & AX_MOD_SHIFT) != 0;

      // Text tool: record position and show text options dialog
      if (tool == ID_TOOL_TEXT) {
        if (!canvas_in_bounds(doc, px, py)) return true;
        IE_DEBUG("text_dialog_open doc=%p at=(%d,%d)", (void *)doc, px, py);
        text_options_t opts;
        memset(&opts, 0, sizeof(opts));
        opts.font_size = g_app->text_tool.font_size;
        opts.color     = g_app->fg_color;
        opts.antialias = g_app->text_tool.antialias;
        if (show_text_dialog(win, &opts) && opts.text[0]) {
          // Persist settings for next use
          g_app->text_tool.font_size = opts.font_size;
          g_app->text_tool.antialias = opts.antialias;
          doc_push_undo(doc);
          if (canvas_draw_text_stb(doc, px, py, &opts)) {
            doc->modified = true;
            doc_update_title(doc);
            invalidate_window(win);
          } else {
            doc_discard_undo(doc);
          }
        }
        return true;
      }

      if (tool == ID_TOOL_MAGIC_WAND) {
        if (!canvas_in_bounds(doc, px, py)) return true;
        if (doc->sel.move.active) canvas_commit_move(doc);
        bool selected = shift
          ? canvas_magic_wand_select_add(doc, px, py,
                                         g_app->wand.spread,
                                         g_app->wand.antialias)
          : canvas_magic_wand_select(doc, px, py,
                                     g_app->wand.spread,
                                     g_app->wand.antialias);
        if (selected) {
          IE_DEBUG("magic_wand_select doc=%p at=(%d,%d) spread=%d aa=%d",
                   (void *)doc, px, py,
                   g_app->wand.spread, g_app->wand.antialias);
          invalidate_window(win);
        }
        return true;
      }

      // Polygon: accumulate vertices on each click; commit on right-click
      if (tool == ID_TOOL_POLYGON) {
        if (!doc->poly.active) {
          doc_push_undo(doc);
          canvas_shape_begin(doc, px, py);  // snapshot for cancel/undo
          doc->poly.active = true;
          doc->poly.count  = 0;
          IE_DEBUG("polygon_begin doc=%p at=(%d,%d)", (void *)doc, px, py);
        }
        if (doc->poly.count < (int)(sizeof(doc->poly.pts)/sizeof(doc->poly.pts[0]))) {
          doc->poly.pts[doc->poly.count++] = (ipoint16_t){px, py};
          IE_DEBUG("polygon_point doc=%p count=%d at=(%d,%d)",
                   (void *)doc, doc->poly.count, px, py);
        }
        doc->last.x = px;
        doc->last.y = py;
        invalidate_window(win);
        return true;
      }

      doc->drawing   = true;
      doc->last.x    = px;
      doc->last.y    = py;
      IE_DEBUG("draw_begin doc=%p tool=%s at=(%d,%d)",
           (void *)doc, tool_id_name(tool), px, py);

      if (canvas_is_shape_tool(tool)) {
        // Shape tools: take snapshot then preview – undo is pushed on mouse-up
        canvas_shape_begin(doc, px, py);
        canvas_shape_preview(doc, px, py, px, py, tool,
                             g_app->shape_filled, g_app->fg_color, g_app->bg_color, false);
        invalidate_window(win);
        return true;
      }

      // Crop tool: only rubber-band the selection — no pixel changes on mouse-down,
      // so no undo snapshot needed here (undo is pushed only on Enter commit).
      if (tool == ID_TOOL_CROP) {
        doc->drawing = true;
        doc->sel.add_mode = false;
        doc->sel.active = false;
        canvas_clear_selection_mask(doc);
        doc->sel.start.x = doc->sel.end.x = px;
        doc->sel.start.y = doc->sel.end.y = py;
        doc->sel.active = true;
        IE_DEBUG("crop_begin doc=%p anchor=(%d,%d)", (void *)doc, px, py);
        invalidate_window(win);
        return true;
      }

      uint8_t *previous_undo = doc->undo.count ? doc->undo.states[doc->undo.count - 1] : NULL;
      doc_push_undo(doc);
      state->stroke_undo = doc->undo.count && doc->undo.states[doc->undo.count - 1] != previous_undo;

      switch (tool) {
        case ID_TOOL_PENCIL:
        case ID_TOOL_BRUSH:
        case ID_TOOL_ERASER: {
          uint32_t color = g_app->fg_color;
          if (tool == ID_TOOL_ERASER) {
#if IMAGEEDITOR_INDEXED
            color = doc->ipal.entries[doc->ipal.transparent];
#else
            color = MAKE_COLOR(0, 0, 0, 0);
#endif
          }
          canvas_stroke_begin(doc, doc_pt, brush_radius(), color, tool == ID_TOOL_BRUSH);
          break;
        }
        case ID_TOOL_FILL:
          canvas_flood_fill(doc, px, py, g_app->fg_color);
          break;
        case ID_TOOL_SPRAY:
          canvas_spray(doc, px, py, 8, g_app->fg_color);
          break;
        case ID_TOOL_MOVE:
          if (selection_move_hit(doc, px, py)) {
            canvas_begin_move(doc, MAKE_COLOR(0, 0, 0, 0));
            float_tex_upload(doc);
            doc->sel.move.origin.x = px;
            doc->sel.move.origin.y = py;
            IE_DEBUG("pixel_move_begin doc=%p at=(%d,%d)", (void *)doc, px, py);
          }
          break;
        case ID_TOOL_SELECT:
          if (!shift && selection_move_hit(doc, px, py)) {
            doc->sel.move.mask_moving = true;
            doc->sel.move.origin.x = px;
            doc->sel.move.origin.y = py;
            IE_DEBUG("selection_mask_move_begin doc=%p at=(%d,%d)", (void *)doc, px, py);
          } else {
            // Start a new selection; commit any in-progress move first.
            if (doc->sel.move.active) canvas_commit_move(doc);
            doc->sel.add_mode = shift;
            if (!doc->sel.add_mode) {
              doc->sel.active = false;
              canvas_clear_selection_mask(doc);
            }
            doc->sel.start.x = doc->sel.end.x = px;
            doc->sel.start.y = doc->sel.end.y = py;
            doc->sel.active = true;
            IE_DEBUG("selection_begin doc=%p anchor=(%d,%d)", (void *)doc, px, py);
          }
          break;
        default:
          break;
      }

      invalidate_window(win);
      doc_update_title(doc);
      return true;
    }

    case evRightButtonDown: {
      // Right-click while polygon is active: commit the polygon
      if (!doc || !g_app) return true;

      // Eyedropper (right click): pick background color from canvas pixel
      if (state && g_app->current_tool == ID_TOOL_EYEDROPPER) {
        int lx = (int16_t)LOWORD(wparam);
        int ly = (int16_t)HIWORD(wparam);
        ipoint16_t doc_pt = ((ipoint16_t){lx, ly});
        int px = doc_pt.x;
        int py = doc_pt.y;
        if (canvas_in_bounds(doc, px, py)) {
          g_app->bg_color = canvas_get_pixel(doc, px, py);
          if (g_app->tool_win)  invalidate_window(g_app->tool_win);
          if (g_app->color_win) invalidate_window(g_app->color_win);
        }
        return true;
      }

      // Zoom tool (right click): zoom out centered on cursor
      if (state && g_app->current_tool == ID_TOOL_ZOOM) {
        int lx = (int16_t)LOWORD(wparam);
        int ly = (int16_t)HIWORD(wparam);
        ipoint16_t doc_pt = {lx, ly};
        int new_scale = -1;
        for (int i = NUM_ZOOM_LEVELS - 1; i >= 0; i--) {
          if (kZoomLevels[i] < window_view_zoom(win)) { new_scale = kZoomLevels[i]; break; }
        }
        if (new_scale > 0)
          apply_zoom_centered(win, state, new_scale, doc_pt);
        return true;
      } else if (g_app->current_tool == ID_TOOL_POLYGON && doc->poly.active && doc->poly.count >= 2) {
        if (g_app->shape_filled)
          canvas_draw_polygon_scaled(doc, doc->poly.pts, doc->poly.count, true,
                                     g_app->fg_color, g_app->bg_color);
        else
          canvas_draw_polygon_scaled(doc, doc->poly.pts, doc->poly.count, false,
                                     g_app->fg_color, g_app->bg_color);
        // Fix up undo: undo_states[top] was pushed with pre-draw pixels from shape_begin.
        // After drawing, swap undo entry (pre-draw) with shape_snapshot (drawn) to align them.
        // Actually the undo was already pushed correctly on first click via doc_push_undo
        // in the polygon start handler; shape_snapshot holds the pre-draw state.
        // We need undo_states[top] = pre-draw, but it currently = pre-draw (correct!).
        // Nothing extra needed — doc_push_undo was called at polygon start.
        IE_DEBUG("polygon_commit doc=%p points=%d", (void *)doc, doc->poly.count);
        doc->poly.active = false;
        doc->poly.count  = 0;
        doc->modified = true;
        doc_update_title(doc);
        invalidate_window(win);
        return true;
      }
      return false;
    }

    case evMouseMove: {
      if (!state || !g_app) return true;

      // Hand tool: update pan while dragging
      if (state->pan.active) {
        window_view_drag(win);
        canvas_sync_scrollbars(win, state);
        invalidate_window(win);
        return true;
      }

      if (!doc) return true;

      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      ipoint16_t doc_pt = {lx, ly};
      int px = doc_pt.x;
      int py = doc_pt.y;

      // Always track the hover position (used by the magnifier overlay)
      state->hover.x    = px;
      state->hover.y    = py;
      state->hover_valid = canvas_in_bounds(doc, px, py);

      // Apply snap for all drawing operations (after hover update so the
      // magnifier loupe always shows the actual pixel under the cursor).
      snap_canvas_pos(&px, &py);

      int tool = g_app->current_tool;
      bool shift = (ui_get_mod_state() & AX_MOD_SHIFT) != 0;
      int status_px = px;
      int status_py = py;
      if (tool == ID_TOOL_SELECT && doc->drawing && doc->sel.active && !doc->sel.move.active) {
        canvas_constrain_tool_drag(tool, shift ? AX_MOD_SHIFT : 0,
                                   doc->sel.start.x, doc->sel.start.y,
                                   &status_px, &status_py);
      }
      status_px /= g_bw_retina_scale;
      status_py /= g_bw_retina_scale;
      canvas_win_update_status(win, status_px, status_py, state->hover_valid);

      // Magnifier: repaint to update the loupe overlay; nothing else to do
      if (tool == ID_TOOL_MAGNIFIER) {
        invalidate_window(win);
        return true;
      }

      // Update polygon rubber-band preview (stores last mouse position in doc->last)
      if (tool == ID_TOOL_POLYGON && doc->poly.active) {
        doc->last.x = px;
        doc->last.y = py;
        invalidate_window(win);
        return true;
      }

      if (!doc->drawing) return true;
      if (px == doc->last.x && py == doc->last.y) return true;

      if (canvas_is_shape_tool(tool)) {
        canvas_shape_preview(doc,
                             doc->shape.start.x, doc->shape.start.y,
                             px, py, tool,
                             g_app->shape_filled, g_app->fg_color, g_app->bg_color, shift);
        doc->last.x = px;
        doc->last.y = py;
        invalidate_window(win);
        return true;
      }

      switch (tool) {
        case ID_TOOL_PENCIL:
        case ID_TOOL_BRUSH:
        case ID_TOOL_ERASER:
          canvas_stroke_drag(doc, (ipoint16_t){px, py});
          break;
        case ID_TOOL_FILL:
          break;
        case ID_TOOL_SPRAY:
          canvas_spray(doc, px, py, 8, g_app->fg_color);
          break;
        case ID_TOOL_MOVE:
          if (doc->sel.move.active) {
            int dx = px - doc->sel.move.origin.x;
            int dy = py - doc->sel.move.origin.y;
            doc->sel.floating.rect.x += dx;
            doc->sel.floating.rect.y += dy;
            doc->sel.move.origin.x = px;
            doc->sel.move.origin.y = py;
          }
          break;
        case ID_TOOL_SELECT:
          if (doc->sel.move.active) {
            int dx = px - doc->sel.move.origin.x;
            int dy = py - doc->sel.move.origin.y;
            doc->sel.floating.rect.x += dx;
            doc->sel.floating.rect.y += dy;
            doc->sel.move.origin.x = px;
            doc->sel.move.origin.y = py;
          } else if (doc->sel.move.mask_moving) {
            int dx = px - doc->sel.move.origin.x;
            int dy = py - doc->sel.move.origin.y;
            canvas_set_selection_mask_offset(doc,
                                             doc->sel.mask.offset.x + dx,
                                             doc->sel.mask.offset.y + dy);
            doc->sel.move.origin.x = px;
            doc->sel.move.origin.y = py;
            invalidate_window(win);
          } else {
            canvas_constrain_tool_drag(tool, shift ? AX_MOD_SHIFT : 0,
                                       doc->sel.start.x, doc->sel.start.y,
                                       &px, &py);
            doc->sel.end.x = px;
            doc->sel.end.y = py;
          }
          break;
        case ID_TOOL_CROP:
          // Update crop rubber-band end; allow coordinates outside canvas bounds
          // so the user can drag beyond the edge to expand.
          doc->sel.end.x = px;
          doc->sel.end.y = py;
          break;
        default:
          break;
      }

      doc->last.x = px;
      doc->last.y = py;
      invalidate_window(win);
      return true;
    }

    case evLeftButtonUp: {
      if (state && state->pan.active) {
        IE_DEBUG("pan_end doc=%p", (void *)doc);
        state->pan.active = false;
      }
      if (!doc || !g_app) return true;
      int tool = g_app->current_tool;

      if (doc->drawing && doc->stroke.active) {
        ipoint16_t point = {(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)};
        canvas_stroke_end(doc, point);
        invalidate_window(win);
      }

      if (canvas_is_shape_tool(tool) && doc->drawing) {
        // Commit the final shape.  doc->pixels already has the drawn result.
        // We need the undo stack to hold the PRE-draw state (shape_snapshot).
        // doc_push_undo() saves the CURRENT pixels; after that we swap the
        // newly pushed entry (= drawn pixels) with shape_snapshot (= pre-draw)
        // so that undo correctly restores the pre-draw state.
        doc_push_undo(doc);
        if (doc->shape.snapshot && doc->undo.count > 0) {
          uint8_t *tmp = doc->undo.states[doc->undo.count - 1];
          doc->undo.states[doc->undo.count - 1] = doc->shape.snapshot;
          doc->shape.snapshot = tmp;  // reuse buffer next time
        }
        doc->drawing  = false;
        doc->modified = true;
        IE_DEBUG("shape_commit doc=%p tool=%s",
                 (void *)doc, tool_id_name(tool));
        doc_update_title(doc);
        invalidate_window(win);
      } else {
        bool was_sel_mask_moving = doc->sel.move.mask_moving;
        if (doc->sel.move.active) {
          canvas_commit_move(doc);
          IE_DEBUG("selection_move_commit doc=%p", (void *)doc);
          doc_update_title(doc);
        }
        if (doc->sel.move.mask_moving) {
          IE_DEBUG("selection_mask_move_commit doc=%p", (void *)doc);
          canvas_commit_selection_mask_offset(doc);
          doc->sel.move.mask_moving = false;
          invalidate_window(win);
        }
        if (tool == ID_TOOL_SELECT && doc->sel.active && !was_sel_mask_moving) {
          IE_DEBUG("selection_end doc=%p from=(%d,%d) to=(%d,%d)",
                   (void *)doc,
                   doc->sel.start.x, doc->sel.start.y,
                   doc->sel.end.x, doc->sel.end.y);
          // A zero-area selection (click without drag) counts as "select nothing".
          if (doc->sel.start.x == doc->sel.end.x &&
              doc->sel.start.y == doc->sel.end.y) {
            if (!doc->sel.add_mode) {
              canvas_deselect(doc);
              IE_DEBUG("selection_deselect_zero_area doc=%p", (void *)doc);
            }
          } else {
            if (doc->sel.add_mode) {
              canvas_select_rect_add(doc,
                                     doc->sel.start.x, doc->sel.start.y,
                                     doc->sel.end.x, doc->sel.end.y);
            } else {
              canvas_select_rect(doc,
                                 doc->sel.start.x, doc->sel.start.y,
                                 doc->sel.end.x, doc->sel.end.y);
            }
          }
          doc->sel.add_mode = false;
        }
        if (tool == ID_TOOL_CROP && doc->sel.active) {
          IE_DEBUG("crop_end doc=%p from=(%d,%d) to=(%d,%d)",
                   (void *)doc,
                   doc->sel.start.x, doc->sel.start.y,
                   doc->sel.end.x, doc->sel.end.y);
          // Zero-area crop: cancel selection.
          if (doc->sel.start.x == doc->sel.end.x &&
              doc->sel.start.y == doc->sel.end.y) {
            canvas_deselect(doc);
            IE_DEBUG("crop_deselect_zero_area doc=%p", (void *)doc);
          }
        }
        if (doc->drawing) {
          IE_DEBUG("draw_end doc=%p tool=%s",
                   (void *)doc, tool_id_name(tool));
        }
        doc->drawing = false;
      }
      return true;
    }

    case evKeyDown: {
      if (!doc || !g_app) return false;
      int tool = g_app->current_tool;
      // Enter commits the crop tool selection (crop or expand canvas).
      if ((wparam == AX_KEY_ENTER || wparam == AX_KEY_KP_ENTER) &&
          tool == ID_TOOL_CROP && doc->sel.active) {
        IE_DEBUG("crop_commit doc=%p sel=(%d,%d)-(%d,%d)",
                 (void *)doc,
                 doc->sel.start.x, doc->sel.start.y,
                 doc->sel.end.x,   doc->sel.end.y);
        doc_push_undo(doc);
        if (canvas_crop_or_expand_to_selection(doc)) {
          canvas_win_sync_scrollbars(win);
          doc_update_title(doc);
          char sb[32];
          snprintf(sb, sizeof(sb), "%dx%d", doc->canvas_w / g_bw_retina_scale, doc->canvas_h / g_bw_retina_scale);
          send_message(doc->win, evStatusBar, 0, sb);
        } else {
          doc_discard_undo(doc);  // crop failed — drop the no-op undo entry
        }
        invalidate_window(win);
        return true;
      }
      // Escape cancels an in-progress polygon or shape drag
      if (wparam == AX_KEY_ESCAPE) {
        if (tool == ID_TOOL_CROP && doc->sel.active) {
          canvas_deselect(doc);
          IE_DEBUG("crop_cancel doc=%p", (void *)doc);
          invalidate_window(win);
          return true;
        }
        if (tool == ID_TOOL_POLYGON && doc->poly.active) {
          if (doc->shape.snapshot) {
            memcpy(doc->pixels, doc->shape.snapshot, (size_t)doc->canvas_w * doc->canvas_h * 4);
            doc->canvas_dirty = true;
          }
          doc_discard_undo(doc);  // drop the no-op undo entry pushed at polygon start
          doc->poly.active = false;
          doc->poly.count  = 0;
          IE_DEBUG("polygon_cancel doc=%p", (void *)doc);
          invalidate_window(win);
          return true;
        }
        if (canvas_is_shape_tool(tool) && doc->drawing && doc->shape.snapshot) {
          memcpy(doc->pixels, doc->shape.snapshot, (size_t)doc->canvas_w * doc->canvas_h * 4);
          doc->canvas_dirty = true;
          doc->drawing = false;
          IE_DEBUG("shape_cancel doc=%p tool=%s",
                   (void *)doc, tool_id_name(tool));
          invalidate_window(win);
          return true;
        }
      }
      return false;
    }

    case evResize: {
      if (state) {
        canvas_sync_scrollbars(win, state);
      }
      return false;
    }

    default:
      return false;
  }
}
