// Form canvas window — renders the design surface, handles element
// selection/move/resize with drag handles, rubber-band placement of new
// controls, and built-in scrollbars when the form is larger than the viewport.

#include "formeditor.h"
#include "../../commctl/commctl.h"

// ============================================================
// Constants
// ============================================================

// Size of each resize-handle square drawn around the selection.
#define HANDLE_SIZE  5
#define HANDLE_HALF  (HANDLE_SIZE / 2)

// Minimum element dimensions after a resize drag.
#define MIN_ELEM_W  10
#define MIN_ELEM_H   8

// Colour of the design-time dot grid.
#define GRID_DOT_COLOR  0xFFA0A0A0

// ============================================================
// Handle indices
//   0=TL  1=TC  2=TR
//   3=ML        4=MR
//   5=BL  6=BC  7=BR
// ============================================================
#define HANDLE_TL    0
#define HANDLE_TC    1
#define HANDLE_TR    2
#define HANDLE_ML    3
#define HANDLE_MR    4
#define HANDLE_BL    5
#define HANDLE_BC    6
#define HANDLE_BR    7
#define HANDLE_COUNT 8

// ============================================================
// Coordinate helpers
// ============================================================

// Convert form-local to canvas-local screen X for a canvas window.
// With child-local evPaint projection (0,0) = canvas window top-left.
static inline int form_to_sx(canvas_state_t *s, int fx) {
  return fx - s->pan.x;
}
static inline int form_to_sy(canvas_state_t *s, int fy) {
  return fy - s->pan.y;
}

// Convert window-local mouse X to form-local.
static inline int local_to_form_x(canvas_state_t *s, int lx) {
  return lx + s->pan.x;
}
static inline int local_to_form_y(canvas_state_t *s, int ly) {
  return ly + s->pan.y;
}

// ============================================================
// Scrollbar helpers  (hscroll on doc_win, vscroll on canvas_win)
// ============================================================
static void canvas_sync_scrollbars(window_t *win, canvas_state_t *s) {
  form_doc_t *doc = s->doc;
  int content_w = doc->form_size.w;
  int content_h = doc->form_size.h;
  // vscroll strip occupies the rightmost SCROLLBAR_WIDTH pixels only when the
  // form is taller than the canvas viewport.
  bool has_vscroll = content_h > win->frame.h;
  int view_w = win->frame.w - (has_vscroll ? SCROLLBAR_WIDTH : 0);
  int view_h = win->frame.h;
  if (view_w < 0) view_w = 0;

  scroll_info_t si;
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  si.nMin  = 0;

  // Horizontal: owned by doc_win (merged with status bar).
  si.nMax  = content_w;
  si.nPage = view_w;
  si.nPos  = s->pan.x;
  set_scroll_info(doc->doc_win, SB_HORZ, &si, false);

  // Vertical: owned by this canvas window.
  si.nMax  = content_h;
  si.nPage = view_h;
  si.nPos  = s->pan.y;
  set_scroll_info(win, SB_VERT, &si, false);
}

static void canvas_clamp_pan(canvas_state_t *s, int win_w, int win_h) {
  form_doc_t *doc = s->doc;
  bool has_vscroll = doc->form_size.h > win_h;
  int view_w = win_w - (has_vscroll ? SCROLLBAR_WIDTH : 0);
  int max_x = MAX(0, doc->form_size.w - view_w);
  int max_y = MAX(0, doc->form_size.h - win_h);
  if (s->pan.x < 0) s->pan.x = 0;
  if (s->pan.y < 0) s->pan.y = 0;
  if (s->pan.x > max_x) s->pan.x = max_x;
  if (s->pan.y > max_y) s->pan.y = max_y;
}

// ============================================================
// Hit testing
// ============================================================

// Return index into doc->elements hit by (lx, ly) in window-local coords,
// or -1 if nothing was hit.  Tests in reverse paint order so topmost wins.
static int hit_test_elements(canvas_state_t *s, int lx, int ly) {
  form_doc_t *doc = s->doc;
  for (int i = doc->element_count - 1; i >= 0; i--) {
    form_element_t *el = &doc->elements[i];
    int ex = form_to_sx(s, el->frame.x);
    int ey = form_to_sy(s, el->frame.y);
    if (lx >= ex && lx < ex + el->frame.w && ly >= ey && ly < ey + el->frame.h)
      return i;
  }
  return -1;
}

// Compute the 8 handle positions (top-left corners) in window-local coords
// for the selected element, stored into out[HANDLE_COUNT].
static void get_handle_rects(canvas_state_t *s, form_element_t *el,
                              int out_x[HANDLE_COUNT], int out_y[HANDLE_COUNT]) {
  int ex = form_to_sx(s, el->frame.x);
  int ey = form_to_sy(s, el->frame.y);
  int ew = el->frame.w;
  int eh = el->frame.h;
  int cx = ex + ew / 2 - HANDLE_HALF;
  int cy = ey + eh / 2 - HANDLE_HALF;

  out_x[HANDLE_TL] = ex - HANDLE_HALF;        out_y[HANDLE_TL] = ey - HANDLE_HALF;
  out_x[HANDLE_TC] = cx;                       out_y[HANDLE_TC] = ey - HANDLE_HALF;
  out_x[HANDLE_TR] = ex + ew - HANDLE_HALF;   out_y[HANDLE_TR] = ey - HANDLE_HALF;
  out_x[HANDLE_ML] = ex - HANDLE_HALF;        out_y[HANDLE_ML] = cy;
  out_x[HANDLE_MR] = ex + ew - HANDLE_HALF;   out_y[HANDLE_MR] = cy;
  out_x[HANDLE_BL] = ex - HANDLE_HALF;        out_y[HANDLE_BL] = ey + eh - HANDLE_HALF;
  out_x[HANDLE_BC] = cx;                       out_y[HANDLE_BC] = ey + eh - HANDLE_HALF;
  out_x[HANDLE_BR] = ex + ew - HANDLE_HALF;   out_y[HANDLE_BR] = ey + eh - HANDLE_HALF;
}

// Return which resize handle (0-7) is under (lx, ly), or -1 if none.
static int hit_test_handles(canvas_state_t *s, int lx, int ly) {
  if (s->selected_idx < 0) return -1;
  form_element_t *el = &s->doc->elements[s->selected_idx];
  int hx[HANDLE_COUNT], hy[HANDLE_COUNT];
  get_handle_rects(s, el, hx, hy);
  for (int i = 0; i < HANDLE_COUNT; i++) {
    if (lx >= hx[i] && lx < hx[i] + HANDLE_SIZE &&
        ly >= hy[i] && ly < hy[i] + HANDLE_SIZE)
      return i;
  }
  return -1;
}

// ============================================================
// Snap helpers
// ============================================================

// Round v to the nearest multiple of grid.
static inline int snap_val(int v, int grid) {
  if (grid <= 1) return v;
  // Correct round-to-nearest for both positive and negative v.
  int half = grid / 2;
  return ((v >= 0) ? (v + half) : (v - half)) / grid * grid;
}

// Snap a form-space coordinate to the document grid (if enabled).
static inline int snap(form_doc_t *doc, int v) {
  return (doc->snap_to_grid && doc->grid_size > 1)
             ? snap_val(v, doc->grid_size) : v;
}

// ============================================================
// Drawing helpers
// ============================================================

// Clamp a rectangle to stay within the form surface bounds.
static irect16_t clamp_to_form(form_doc_t *doc, irect16_t r) {
  if (r.x < 0) { r.w += r.x; r.x = 0; }
  if (r.y < 0) { r.h += r.y; r.y = 0; }
  if (r.x + r.w > doc->form_size.w) r.w = doc->form_size.w - r.x;
  if (r.y + r.h > doc->form_size.h) r.h = doc->form_size.h - r.y;
  if (r.w < 1) r.w = 1;
  if (r.h < 1) r.h = 1;
  return r;
}

static void draw_handles(window_t *win, canvas_state_t *s);
static void draw_rubber_band(window_t *win, canvas_state_t *s);
static void canvas_create_overlay(window_t *canvas_win, canvas_state_t *s);

// Design-time interaction layer.
//
// Classic VB/ActiveX designers treated hosted controls as components inside a
// container-managed design surface: the IDE/container owned selection,
// drag/resize handles, and design-mode mouse interaction, while the control
// mostly contributed its visual representation and used container/ambient
// state (for example UserMode) to distinguish design-time from runtime.
//
// We mirror that approach here by placing a transparent overlay above all live
// controls. The real controls still paint themselves underneath, but the
// overlay is the only child that participates in hit-testing, so design-time
// input is handled consistently by the form editor rather than by each control.
static result_t live_overlay_proc(window_t *win, uint32_t msg,
                                  uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      win->userdata = lparam;
      win->notabstop = false;
      return true;
    case evPaint: {
      canvas_state_t *s = (canvas_state_t *)win->userdata;
      if (!s) return true;
      draw_handles(win, s);
      draw_rubber_band(win, s);
      return true;
    }
    case evLeftButtonDown:
    case evLeftButtonDoubleClick:
    case evLeftButtonUp:
    case evMouseMove:
    case evWheel:
    case evKeyDown:
    case evKeyUp:
    case evSetFocus:
      return send_message(win->parent, msg, wparam, lparam);
    default:
      return false;
  }
}

static winproc_t ctrl_type_to_proc(int type) {
  switch (type) {
    case CTRL_BUTTON:   return win_button;
    case CTRL_CHECKBOX: return win_checkbox;
    case CTRL_LABEL:    return win_label;
    case CTRL_TEXTEDIT: return win_textedit;
    case CTRL_LIST:     return win_list;
    case CTRL_COMBOBOX: return win_combobox;
    default:            return NULL;
  }
}

static form_element_t *canvas_find_element_for_live_window(window_t *win) {
  canvas_state_t *s;
  form_doc_t *doc;
  if (!win || !win->parent) return NULL;
  s = (canvas_state_t *)win->parent->userdata;
  if (!s) return NULL;
  doc = s->doc;
  if (!doc) return NULL;
  for (int i = 0; i < doc->element_count; i++) {
    if (doc->elements[i].live_win == win)
      return &doc->elements[i];
  }
  return NULL;
}

static bool canvas_child_window_alive(window_t *root, window_t *target) {
  if (!root || !target) return false;
  if (root == target) return true;
  for (window_t *child = root->children; child; child = child->next) {
    if (canvas_child_window_alive(child, target))
      return true;
  }
  for (window_t *child = root->toolbar_children; child; child = child->next) {
    if (canvas_child_window_alive(child, target))
      return true;
  }
  return false;
}

static result_t design_live_ctrl_proc(window_t *win, uint32_t msg,
                                      uint32_t wparam, void *lparam) {
  form_element_t *el = canvas_find_element_for_live_window(win);
  winproc_t real_proc = el ? ctrl_type_to_proc(el->type) : NULL;

  switch (msg) {
    case evCreate: {
      form_element_t *creating_el = (form_element_t *)lparam;
      if (!real_proc && creating_el)
        real_proc = ctrl_type_to_proc(creating_el->type);
      win->notabstop = true;
      return real_proc ? real_proc(win, msg, wparam, NULL) : true;
    }
    case evDestroy:
    case evPaint:
    case evResize:
      return real_proc ? real_proc(win, msg, wparam, lparam) : false;
    case evLeftButtonDown:
    case evLeftButtonDoubleClick:
    case evLeftButtonUp:
    case evRightButtonDown:
    case evRightButtonUp:
    case evMouseMove: {
      if (win->parent) {
        int lx = (int16_t)LOWORD(wparam);
        int ly = (int16_t)HIWORD(wparam);
        return send_message(win->parent, msg,
                            MAKEDWORD(win->frame.x + lx, win->frame.y + ly),
                            lparam);
      }
      return true;
    }
    case evKeyDown:
    case evKeyUp:
    case evCommand:
    case evSetFocus:
      return true;
    default:
      return false;
  }
}

static result_t preview_ctrl_proc(window_t *win, uint32_t msg,
                                  uint32_t wparam, void *lparam) {
  canvas_state_t *s = (win && win->parent) ? (canvas_state_t *)win->parent->userdata : NULL;
  int type = s ? s->preview_type : -1;
  winproc_t real_proc = ctrl_type_to_proc(type);

  switch (msg) {
    case evCreate:
      win->notabstop = true;
      return real_proc ? real_proc(win, msg, wparam, NULL) : true;
    case evDestroy:
    case evPaint:
    case evResize:
      return real_proc ? real_proc(win, msg, wparam, lparam) : false;
    case evLeftButtonDown:
    case evLeftButtonDoubleClick:
    case evLeftButtonUp:
    case evRightButtonDown:
    case evRightButtonUp:
    case evMouseMove:
      if (win->parent) {
        int lx = (int16_t)LOWORD(wparam);
        int ly = (int16_t)HIWORD(wparam);
        return send_message(win->parent, msg,
                            MAKEDWORD(win->frame.x + lx, win->frame.y + ly),
                            lparam);
      }
      return true;
    case evKeyDown:
    case evKeyUp:
    case evCommand:
    case evSetFocus:
      return true;
    default:
      return false;
  }
}

static void canvas_sync_overlay_frame(form_doc_t *doc) {
  canvas_state_t *s;
  if (!doc || !doc->canvas_win) return;
  s = (canvas_state_t *)doc->canvas_win->userdata;
  if (!s || !canvas_child_window_alive(doc->canvas_win, s->overlay_win)) return;
  move_window(s->overlay_win, 0, 0);
  resize_window(s->overlay_win, doc->canvas_win->frame.w, doc->canvas_win->frame.h);
}

static void canvas_destroy_preview(canvas_state_t *s) {
  if (!s) return;
  if (s->doc && s->doc->canvas_win &&
      canvas_child_window_alive(s->doc->canvas_win, s->preview_win))
    destroy_window(s->preview_win);
  s->preview_win = NULL;
  s->preview_type = -1;
}

static void canvas_reset_drag(canvas_state_t *s) {
  if (!s) return;
  canvas_destroy_preview(s);
  s->drag_mode = DRAG_NONE;
  s->drag_handle = -1;
  s->placing_type = -1;
  set_capture(NULL);
}

static void canvas_set_select_tool(void) {
  if (!g_app) return;
  g_app->current_tool = ID_TOOL_SELECT;
  if (g_app->tool_win)
    send_message(g_app->tool_win, bxSetActiveItem,
                 (uint32_t)ID_TOOL_SELECT, NULL);
}

static void canvas_cancel_drag(canvas_state_t *s) {
  bool was_placing = s && s->drag_mode == DRAG_RUBBERBND;
  canvas_reset_drag(s);
  if (was_placing)
    canvas_set_select_tool();
}

static void canvas_update_preview(canvas_state_t *s, int type, int x, int y, int w, int h,
                                  const char *text, uint32_t flags) {
  form_doc_t *doc;
  if (!s || !s->doc || !s->doc->canvas_win) return;
  doc = s->doc;
  if (type < 0 || !ctrl_type_to_proc(type)) return;

  if (!canvas_child_window_alive(doc->canvas_win, s->preview_win) ||
      s->preview_type != type) {
    canvas_destroy_preview(s);
    s->preview_type = type;
    s->preview_win = create_window(text ? text : "", flags,
                                   MAKERECT(0, 0, MAX(w, 1), MAX(h, 1)),
                                   doc->canvas_win, preview_ctrl_proc, 0, NULL);
    if (!s->preview_win) return;
    s->preview_win->notabstop = true;
    canvas_create_overlay(doc->canvas_win, s);
  }

  move_window(s->preview_win, form_to_sx(s, x), form_to_sy(s, y));
  resize_window(s->preview_win, MAX(w, 1), MAX(h, 1));
  if (text && strcmp(s->preview_win->title, text) != 0) {
    snprintf(s->preview_win->title, sizeof(s->preview_win->title), "%s", text);
    invalidate_window(s->preview_win);
  }
}

static void canvas_create_overlay(window_t *canvas_win, canvas_state_t *s) {
  if (!canvas_win || !s) return;
  if (canvas_child_window_alive(canvas_win, s->overlay_win))
    destroy_window(s->overlay_win);
  s->overlay_win = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL,
                                 MAKERECT(0, 0, canvas_win->frame.w, canvas_win->frame.h),
                                 canvas_win, live_overlay_proc, 0, s);
  if (s->overlay_win) s->overlay_win->notabstop = false;
}

static void canvas_sync_live_element_window(form_doc_t *doc, form_element_t *el) {
  canvas_state_t *s;
  if (!doc || !doc->canvas_win || !el ||
      !canvas_child_window_alive(doc->canvas_win, el->live_win))
    return;
  s = (canvas_state_t *)doc->canvas_win->userdata;
  if (!s) return;
  move_window(el->live_win, form_to_sx(s, el->frame.x), form_to_sy(s, el->frame.y));
  resize_window(el->live_win, el->frame.w, el->frame.h);
  if (strcmp(el->live_win->title, el->text) != 0) {
    snprintf(el->live_win->title, sizeof(el->live_win->title), "%s", el->text);
    invalidate_window(el->live_win);
  }
}

static void canvas_create_live_element_window(form_doc_t *doc, form_element_t *el) {
  canvas_state_t *s;
  if (!doc || !doc->canvas_win || !el) return;
  s = (canvas_state_t *)doc->canvas_win->userdata;
  if (!s) return;
  if (!ctrl_type_to_proc(el->type)) return;
  el->live_win = create_window(el->text, el->flags,
                               MAKERECT(form_to_sx(s, el->frame.x), form_to_sy(s, el->frame.y),
                                        el->frame.w, el->frame.h),
                               doc->canvas_win, design_live_ctrl_proc, 0, el);
  if (!el->live_win) return;
  el->live_win->id = el->id;
  el->live_win->notabstop = true;
}

void canvas_sync_live_controls(form_doc_t *doc) {
  if (!doc || !doc->canvas_win) return;
  for (int i = 0; i < doc->element_count; i++)
    canvas_sync_live_element_window(doc, &doc->elements[i]);
  canvas_sync_overlay_frame(doc);
  invalidate_window(doc->canvas_win);
}

void canvas_rebuild_live_controls(form_doc_t *doc) {
  canvas_state_t *s;
  if (!doc || !doc->canvas_win) return;
  s = (canvas_state_t *)doc->canvas_win->userdata;
  if (!s) return;

  while (doc->canvas_win->children)
    destroy_window(doc->canvas_win->children);

  for (int i = 0; i < doc->element_count; i++) {
    doc->elements[i].live_win = NULL;
    canvas_create_live_element_window(doc, &doc->elements[i]);
  }

  canvas_create_overlay(doc->canvas_win, s);
  canvas_sync_live_controls(doc);
}

// Draw the 8 resize handles around the selected element.
static void draw_handles(window_t *win, canvas_state_t *s) {
  (void)win;
  if (s->selected_idx < 0) return;
  form_element_t *el = &s->doc->elements[s->selected_idx];
  int hx[HANDLE_COUNT], hy[HANDLE_COUNT];
  get_handle_rects(s, el, hx, hy);

  // Dotted selection border (4-pixel segments, screen coords)
  int bx = form_to_sx(s, el->frame.x) - 1;
  int by = form_to_sy(s, el->frame.y) - 1;
  int bw = el->frame.w + 2;
  int bh = el->frame.h + 2;
  draw_sel_rect(R(bx, by, bw, bh));

  // Solid handle squares
  uint32_t hcol = 0xFF000000;
  for (int i = 0; i < HANDLE_COUNT; i++)
    fill_rect(hcol, R(hx[i], hy[i], HANDLE_SIZE, HANDLE_SIZE));
}

// Draw a rubber-band rectangle (for placement drag) in form coords.
static void draw_rubber_band(window_t *win, canvas_state_t *s) {
  (void)win;
  if (s->drag_mode != DRAG_RUBBERBND) return;
  int x0 = s->rb.x < 0 ? 0 : s->rb.x;
  int y0 = s->rb.y < 0 ? 0 : s->rb.y;
  int x1 = x0 + s->rb.w;
  int y1 = y0 + s->rb.h;
  if (x1 > s->doc->form_size.w) x1 = s->doc->form_size.w;
  if (y1 > s->doc->form_size.h) y1 = s->doc->form_size.h;
  if (x1 <= x0 || y1 <= y0) return;
  int sx = form_to_sx(s, x0);
  int sy = form_to_sy(s, y0);
  draw_sel_rect(R(sx, sy, x1 - x0, y1 - y0));
}

static uint32_t g_grid_dot_tex = 0;
static int      g_grid_dot_tex_size = 0;

static uint32_t ensure_grid_dot_texture(int grid) {
  if (g_grid_dot_tex != 0 && g_grid_dot_tex_size == grid)
    return g_grid_dot_tex;

  if (g_grid_dot_tex != 0) {
    R_DeleteTexture(g_grid_dot_tex);
    g_grid_dot_tex = 0;
    g_grid_dot_tex_size = 0;
  }

  size_t pixel_count = (size_t)grid * (size_t)grid;
  uint8_t *pixels = (uint8_t *)calloc(pixel_count, 4);
  if (!pixels)
    return 0;

  pixels[0] = 255;
  pixels[1] = 255;
  pixels[2] = 255;
  pixels[3] = 255;
  g_grid_dot_tex = R_CreateTextureRGBA(grid, grid, pixels,
                                       R_FILTER_NEAREST, R_WRAP_REPEAT);
  free(pixels);
  if (g_grid_dot_tex != 0)
    g_grid_dot_tex_size = grid;
  return g_grid_dot_tex;
}

static void free_grid_dot_texture(void) {
  if (g_grid_dot_tex == 0)
    return;
  R_DeleteTexture(g_grid_dot_tex);
  g_grid_dot_tex = 0;
  g_grid_dot_tex_size = 0;
}

// Draw the design-time dot grid with one repeat-wrapped texture draw.
// The texture is grid x grid pixels with a single opaque dot at (0,0).
static void draw_grid(canvas_state_t *s, int fx, int fy, int fw, int fh) {
  form_doc_t *doc = s->doc;
  if (!doc->show_grid) return;
  int grid = doc->grid_size;
  if (grid <= 1) return;  // grid=1 would paint every pixel; skip for performance
  uint32_t tex = ensure_grid_dot_texture(grid);
  if (tex == 0) return;
  draw_sprite_region((int)tex, R(fx, fy, fw, fh),
                     UV_RECT(0.0f, 0.0f,
                             (float)fw / (float)grid,
                             (float)fh / (float)grid),
                     GRID_DOT_COLOR, 0);
}

// ============================================================
// Tool -> control type mapping
// ============================================================
static int tool_to_ctrl_type(int tool) {
  switch (tool) {
    case ID_TOOL_BUTTON:   return CTRL_BUTTON;
    case ID_TOOL_CHECKBOX: return CTRL_CHECKBOX;
    case ID_TOOL_LABEL:    return CTRL_LABEL;
    case ID_TOOL_TEXTEDIT: return CTRL_TEXTEDIT;
    case ID_TOOL_LIST:     return CTRL_LIST;
    case ID_TOOL_COMBOBOX: return CTRL_COMBOBOX;
    default:               return -1;
  }
}

// Default dimensions for newly placed controls.
static isize16_t default_ctrl_size(int type) {
  isize16_t size;
  switch (type) {
    case CTRL_BUTTON:   size = (isize16_t){75, 23};  break;
    case CTRL_CHECKBOX: size = (isize16_t){97, 17};  break;
    case CTRL_LABEL:    size = (isize16_t){65, 13};  break;
    case CTRL_TEXTEDIT: size = (isize16_t){121, 20}; break;
    case CTRL_LIST:     size = (isize16_t){121, 60}; break;
    case CTRL_COMBOBOX: size = (isize16_t){121, 20}; break;
    default:            size = (isize16_t){80, 20};  break;
  }
  return size;
}

// Control type display names for use in caption and name generation.
static const char *ctrl_type_name(int type) {
  switch (type) {
    case CTRL_BUTTON:   return "Button";
    case CTRL_CHECKBOX: return "CheckBox";
    case CTRL_LABEL:    return "Label";
    case CTRL_TEXTEDIT: return "TextBox";
    case CTRL_LIST:     return "ListBox";
    case CTRL_COMBOBOX: return "ComboBox";
    default:            return "Control";
  }
}

// ============================================================
// Add a new element to the document
// ============================================================
static int canvas_add_element(form_doc_t *doc, int type, irect16_t frame) {
  if (doc->element_count >= MAX_ELEMENTS) return -1;
  if (type < 0 || type >= CTRL_TYPE_COUNT) return -1;
  if (frame.w < MIN_ELEM_W) frame.w = MIN_ELEM_W;
  if (frame.h < MIN_ELEM_H) frame.h = MIN_ELEM_H;

  int index = doc->element_count;
  form_element_t *el = &doc->elements[index];
  el->type  = type;
  el->id    = doc->next_id++;
  el->frame = frame;
  el->flags = 0;

  int n = ++doc->type_counters[type];
  // Caption (text shown inside the control)
  snprintf(el->text, sizeof(el->text), "%s%d", ctrl_type_name(type), n);
  // Identifier name (used in .h file)
  char pfx[8];
  switch (type) {
    case CTRL_BUTTON:   strncpy(pfx, "IDC_BTN",  sizeof(pfx) - 1); break;
    case CTRL_CHECKBOX: strncpy(pfx, "IDC_CHK",  sizeof(pfx) - 1); break;
    case CTRL_LABEL:    strncpy(pfx, "IDC_LBL",  sizeof(pfx) - 1); break;
    case CTRL_TEXTEDIT: strncpy(pfx, "IDC_EDT",  sizeof(pfx) - 1); break;
    case CTRL_LIST:     strncpy(pfx, "IDC_LST",  sizeof(pfx) - 1); break;
    case CTRL_COMBOBOX: strncpy(pfx, "IDC_CMB",  sizeof(pfx) - 1); break;
    default:            strncpy(pfx, "IDC_CTRL", sizeof(pfx) - 1); break;
  }
  pfx[sizeof(pfx)-1] = '\0';
  snprintf(el->name, sizeof(el->name), "%s%d", pfx, n);

  doc->element_count++;
  canvas_state_t *s = doc->canvas_win ? (canvas_state_t *)doc->canvas_win->userdata : NULL;
  if (s)
    s->selected_idx = index;
  canvas_create_live_element_window(doc, el);
  canvas_create_overlay(doc->canvas_win, (canvas_state_t *)doc->canvas_win->userdata);
  canvas_sync_live_controls(doc);
  doc->modified = true;
  form_doc_update_title(doc);
  return index;
}

static void canvas_preview_label(int type, int index, char *text, size_t text_sz) {
  const char *base = ctrl_type_name(type);
  if (!text || text_sz == 0) return;
  snprintf(text, text_sz, "%s%d", base, index);
}

static void canvas_update_rubber_band(canvas_state_t *s, int lx, int ly) {
  form_doc_t *doc = s->doc;
  int fx = local_to_form_x(s, lx);
  int fy = local_to_form_y(s, ly);
  int ox = local_to_form_x(s, s->drag_start.x);
  int oy = local_to_form_y(s, s->drag_start.y);
  int x0 = snap(doc, ox < fx ? ox : fx);
  int y0 = snap(doc, oy < fy ? oy : fy);
  int x1 = snap(doc, ox < fx ? fx : ox);
  int y1 = snap(doc, oy < fy ? fy : oy);
  s->rb = (irect16_t){x0, y0, x1 - x0, y1 - y0};
}

static void canvas_update_placement_preview(canvas_state_t *s) {
  form_doc_t *doc = s->doc;
  int ctrl_type = s->placing_type;
  char preview_text[64];
  if (ctrl_type < 0) return;
  irect16_t preview = s->rb;
  if (preview.w < MIN_ELEM_W || preview.h < MIN_ELEM_H) {
    isize16_t size = default_ctrl_size(ctrl_type);
    preview.w = size.w;
    preview.h = size.h;
  }
  canvas_preview_label(ctrl_type, doc->type_counters[ctrl_type] + 1,
                       preview_text, sizeof(preview_text));
  canvas_update_preview(s, ctrl_type, preview.x, preview.y,
                        preview.w, preview.h, preview_text, 0);
}

// ============================================================
// Apply resize delta to the selected element
// ============================================================
static void canvas_apply_resize(canvas_state_t *s, int dx, int dy) {
  form_doc_t     *doc = s->doc;
  form_element_t *el  = &doc->elements[s->selected_idx];
  int x = s->snap_rect.x, y = s->snap_rect.y, w = s->snap_rect.w, h = s->snap_rect.h;

  switch (s->drag_handle) {
    case HANDLE_TL: x += dx; y += dy; w -= dx; h -= dy; break;
    case HANDLE_TC:           y += dy;           h -= dy; break;
    case HANDLE_TR:           y += dy; w += dx;  h -= dy; break;
    case HANDLE_ML: x += dx;           w -= dx;           break;
    case HANDLE_MR:           w += dx;                    break;
    case HANDLE_BL: x += dx;           w -= dx; h += dy;  break;
    case HANDLE_BC:                              h += dy;  break;
    case HANDLE_BR:           w += dx;           h += dy;  break;
    default: break;
  }

  // Snap the resulting geometry to the grid.
  if (doc->snap_to_grid && doc->grid_size > 1) {
    int g = doc->grid_size;
    // Snap each edge that the handle touches; keep the opposite edge fixed.
    bool moves_left  = (s->drag_handle == HANDLE_TL || s->drag_handle == HANDLE_ML || s->drag_handle == HANDLE_BL);
    bool moves_top   = (s->drag_handle == HANDLE_TL || s->drag_handle == HANDLE_TC || s->drag_handle == HANDLE_TR);
    bool moves_right = (s->drag_handle == HANDLE_TR || s->drag_handle == HANDLE_MR || s->drag_handle == HANDLE_BR);
    bool moves_bot   = (s->drag_handle == HANDLE_BL || s->drag_handle == HANDLE_BC || s->drag_handle == HANDLE_BR);
    int sx = moves_left  ? snap_val(x, g)     : x;
    int sy = moves_top   ? snap_val(y, g)     : y;
    int rx = moves_right ? snap_val(x + w, g) : x + w;
    int by = moves_bot   ? snap_val(y + h, g) : y + h;
    x = sx; y = sy; w = rx - sx; h = by - sy;
  }

  if (w < MIN_ELEM_W) {
    if (s->drag_handle == HANDLE_TL || s->drag_handle == HANDLE_ML ||
        s->drag_handle == HANDLE_BL) x = s->snap_rect.x + s->snap_rect.w - MIN_ELEM_W;
    w = MIN_ELEM_W;
  }
  if (h < MIN_ELEM_H) {
    if (s->drag_handle == HANDLE_TL || s->drag_handle == HANDLE_TC ||
        s->drag_handle == HANDLE_TR) y = s->snap_rect.y + s->snap_rect.h - MIN_ELEM_H;
    h = MIN_ELEM_H;
  }
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  el->frame.x = x; el->frame.y = y; el->frame.w = w; el->frame.h = h;
}

// ============================================================
// Window procedure
// ============================================================
result_t win_canvas_proc(window_t *win, uint32_t msg,
                          uint32_t wparam, void *lparam) {
  canvas_state_t *s = (canvas_state_t *)win->userdata;
  form_doc_t *doc = s ? s->doc : NULL;

  switch (msg) {
    case evCreate: {
      canvas_state_t *st = allocate_window_data(win, sizeof(canvas_state_t));
      st->doc          = (form_doc_t *)lparam;
      st->preview_type = -1;
      st->placing_type = -1;
      st->selected_idx = -1;
      st->pan          = (ipoint16_t){0, 0};
      st->drag_mode    = DRAG_NONE;
      st->drag_handle  = -1;
      canvas_sync_scrollbars(win, st);
      canvas_rebuild_live_controls(st->doc);
      return true;
    }

    case evDestroy:
      canvas_destroy_preview(s);
      free_grid_dot_texture();
      // win->userdata freed by the framework via allocate_window_data.
      return false;

    case evSetFocus:
      return false;

    case evResize: {
      if (!s) return false;
      canvas_clamp_pan(s, win->frame.w, win->frame.h);
      canvas_sync_scrollbars(win, s);
      canvas_sync_live_controls(doc);
      return false;
    }

    case evHScroll: {
      if (!s) return false;
      s->pan.x = (int)wparam;
      canvas_clamp_pan(s, win->frame.w, win->frame.h);
      canvas_sync_scrollbars(win, s);
      canvas_sync_live_controls(doc);
      return true;
    }

    case evVScroll: {
      if (!s) return false;
      s->pan.y = (int)wparam;
      canvas_clamp_pan(s, win->frame.w, win->frame.h);
      canvas_sync_scrollbars(win, s);
      canvas_sync_live_controls(doc);
      return true;
    }

    case evPaint: {
      if (!s || !doc) return true;

      // Dark workspace background
      fill_rect(get_sys_color(brWorkspaceBg),
                R(0, 0, win->frame.w, win->frame.h));

      // Form surface (window-colored rectangle with a 1px dark border)
      int fx = form_to_sx(s, 0);
      int fy = form_to_sy(s, 0);
      int fw = doc->form_size.w;
      int fh = doc->form_size.h;
      fill_rect(get_sys_color(brWindowBg), R(fx, fy, fw, fh));
      fill_rect(get_sys_color(brDarkEdge), R(fx - 1, fy - 1, fw + 2, 1));
      fill_rect(get_sys_color(brDarkEdge), R(fx - 1, fy - 1, 1, fh + 2));
      fill_rect(get_sys_color(brDarkEdge), R(fx - 1, fy + fh, fw + 2, 1));
      fill_rect(get_sys_color(brDarkEdge), R(fx + fw, fy - 1, 1, fh + 2));

      // Dot grid on the form surface
      draw_grid(s, fx, fy, fw, fh);

      return false;
    }

    case evLeftButtonDown: {
      if (!s || !doc) return false;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      int tool = g_app ? g_app->current_tool : ID_TOOL_SELECT;

      if (s->drag_mode != DRAG_NONE) {
        canvas_cancel_drag(s);
        canvas_sync_live_controls(doc);
        return true;
      }

      if (tool == ID_TOOL_SELECT) {
        // Check resize handles first
        int handle = hit_test_handles(s, lx, ly);
        if (handle >= 0 && s->selected_idx >= 0) {
          form_element_t *el = &doc->elements[s->selected_idx];
          s->drag_mode       = DRAG_RESIZE;
          s->drag_handle     = handle;
          s->drag_start      = (ipoint16_t){lx, ly};
          s->snap_rect       = el->frame;
          set_capture(win);
          return true;
        }
        // Hit test elements
        int hit = hit_test_elements(s, lx, ly);
        s->selected_idx = hit;
        if (hit >= 0) {
          form_element_t *el = &doc->elements[hit];
          s->drag_mode   = DRAG_MOVE;
          s->drag_start      = (ipoint16_t){lx, ly};
          s->snap_rect.x    = el->frame.x;
          s->snap_rect.y    = el->frame.y;
          set_capture(win);
        } else {
          s->drag_mode = DRAG_NONE;
        }
        invalidate_window(win);
        return true;
      }

      // Placement tools: start rubber-band drag
      int ctrl_type = tool_to_ctrl_type(tool);
      if (ctrl_type >= 0) {
        int fx = local_to_form_x(s, lx);
        int fy = local_to_form_y(s, ly);
        char preview_text[64];
        // Clamp to form surface
        if (fx < 0) fx = 0;
        if (fy < 0) fy = 0;
        if (fx > doc->form_size.w) fx = doc->form_size.w;
        if (fy > doc->form_size.h) fy = doc->form_size.h;
        s->drag_mode   = DRAG_RUBBERBND;
        s->drag_start  = (ipoint16_t){lx, ly};
        s->rb          = (irect16_t){fx, fy, 0, 0};
        s->placing_type = ctrl_type;
        s->selected_idx = -1;
        canvas_preview_label(ctrl_type, doc->type_counters[ctrl_type] + 1,
                             preview_text, sizeof(preview_text));
        canvas_update_preview(s, ctrl_type, fx, fy, 1, 1,
                              preview_text, 0);
        set_capture(win);
        return true;
      }
      return false;
    }

    case evMouseMove: {
      if (!s) return false;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);

      if (s->drag_mode == DRAG_MOVE && s->selected_idx >= 0) {
        form_element_t *el = &doc->elements[s->selected_idx];
        int nx = snap(doc, s->snap_rect.x + (lx - s->drag_start.x));
        int ny = snap(doc, s->snap_rect.y + (ly - s->drag_start.y));
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (nx + el->frame.w > doc->form_size.w) nx = doc->form_size.w - el->frame.w;
        if (ny + el->frame.h > doc->form_size.h) ny = doc->form_size.h - el->frame.h;
        el->frame.x = nx;
        el->frame.y = ny;
        canvas_sync_live_controls(doc);
        invalidate_window(win);
        return true;
      }

      if (s->drag_mode == DRAG_RESIZE && s->selected_idx >= 0) {
        int dx = lx - s->drag_start.x;
        int dy = ly - s->drag_start.y;
        canvas_apply_resize(s, dx, dy);
        doc->modified = true;
        canvas_sync_live_controls(doc);
        invalidate_window(win);
        return true;
      }

      if (s->drag_mode == DRAG_RUBBERBND) {
        canvas_update_rubber_band(s, lx, ly);
        canvas_update_placement_preview(s);
        canvas_sync_live_controls(doc);
        invalidate_window(win);
        return true;
      }
      invalidate_window(win);
      return false;
    }

    case evLeftButtonUp: {
      if (!s) return false;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      (void)lx; (void)ly;

      if (s->drag_mode == DRAG_MOVE && s->selected_idx >= 0) {
        doc->modified = true;
        form_doc_update_title(doc);
      }

      if (s->drag_mode == DRAG_RESIZE) {
        doc->modified = true;
        form_doc_update_title(doc);
      }

      if (s->drag_mode == DRAG_RUBBERBND) {
        int ctrl_type = s->placing_type;
        if (ctrl_type >= 0) {
          canvas_update_rubber_band(s, lx, ly);
          irect16_t frame = s->rb;
          // If no drag (click only), use the default size for the control.
          if (frame.w < MIN_ELEM_W || frame.h < MIN_ELEM_H) {
            isize16_t size = default_ctrl_size(ctrl_type);
            frame.w = size.w;
            frame.h = size.h;
          }
          frame = clamp_to_form(doc, frame);
          canvas_add_element(doc, ctrl_type, frame);
        }
        // Revert to Select tool after placing
        canvas_set_select_tool();
      }

      canvas_reset_drag(s);
      canvas_sync_live_controls(doc);
      return true;
    }

    case evKeyDown: {
      if (!s) return false;
      // Del key deletes the selected element (redundant with accelerator,
      // but handles canvas-focused case when menubar proc isn't active).
      uint32_t key = wparam;
      if ((key == AX_KEY_DEL || key == AX_KEY_BACKSPACE) && s->selected_idx >= 0) {
        handle_menu_command(ID_EDIT_DELETE);
        return true;
      }
      return false;
    }

    case evWheel: {
      if (!s) return false;
      int delta = (int)(int16_t)LOWORD(wparam);
      s->pan.y -= delta * SCROLL_SENSITIVITY;
      canvas_clamp_pan(s, win->frame.w, win->frame.h);
      canvas_sync_scrollbars(win, s);
      canvas_sync_live_controls(doc);
      return true;
    }

    default:
      return false;
  }
}
