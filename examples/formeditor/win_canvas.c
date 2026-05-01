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
#define HANDLE_HIT_OUTSET 3

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

static inline canvas_pt_t form_to_canvas_pt(canvas_state_t *s, form_pt_t p) {
  return (canvas_pt_t){p.x - s->pan.x, p.y - s->pan.y};
}

static inline form_pt_t canvas_to_form_pt(canvas_state_t *s, canvas_pt_t p) {
  return (form_pt_t){p.x + s->pan.x, p.y + s->pan.y};
}

static inline irect16_t form_to_canvas_rect(canvas_state_t *s, irect16_t r) {
  canvas_pt_t p = form_to_canvas_pt(s, (form_pt_t){r.x, r.y});
  return (irect16_t){p.x, p.y, r.w, r.h};
}

static void canvas_set_draw_space(window_t *win) {
  window_t *root = get_root_window(win);
  int t = titlebar_height(root);
  int cx = win->parent ? win->frame.x : 0;
  int cy = win->parent ? win->frame.y : 0;

  set_viewport(root->frame);
  set_projection(root->scroll[0] - cx,
                 -t - cy + root->scroll[1],
                 root->frame.w + root->scroll[0] - cx,
                 root->frame.h - t - cy + root->scroll[1]);
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
    irect16_t r = form_to_canvas_rect(s, el->frame);
    if (lx >= r.x && lx < r.x + r.w && ly >= r.y && ly < r.y + r.h)
      return i;
  }
  return -1;
}

// Compute the 8 handle positions (top-left corners) in window-local coords
// for the selected element, stored into out[HANDLE_COUNT].
static void get_handle_rects(canvas_state_t *s, form_element_t *el,
                              int out_x[HANDLE_COUNT], int out_y[HANDLE_COUNT]) {
  irect16_t r = form_to_canvas_rect(s, el->frame);
  int cx = r.x + r.w / 2 - HANDLE_HALF;
  int cy = r.y + r.h / 2 - HANDLE_HALF;

  out_x[HANDLE_TL] = r.x - HANDLE_HALF;        out_y[HANDLE_TL] = r.y - HANDLE_HALF;
  out_x[HANDLE_TC] = cx;                       out_y[HANDLE_TC] = r.y - HANDLE_HALF;
  out_x[HANDLE_TR] = r.x + r.w - HANDLE_HALF;  out_y[HANDLE_TR] = r.y - HANDLE_HALF;
  out_x[HANDLE_ML] = r.x - HANDLE_HALF;        out_y[HANDLE_ML] = cy;
  out_x[HANDLE_MR] = r.x + r.w - HANDLE_HALF;  out_y[HANDLE_MR] = cy;
  out_x[HANDLE_BL] = r.x - HANDLE_HALF;        out_y[HANDLE_BL] = r.y + r.h - HANDLE_HALF;
  out_x[HANDLE_BC] = cx;                       out_y[HANDLE_BC] = r.y + r.h - HANDLE_HALF;
  out_x[HANDLE_BR] = r.x + r.w - HANDLE_HALF;  out_y[HANDLE_BR] = r.y + r.h - HANDLE_HALF;
}

// Return which resize handle (0-7) is under (lx, ly), or -1 if none.
static int hit_test_handles(canvas_state_t *s, int lx, int ly) {
  if (s->selected_idx < 0) return -1;
  form_element_t *el = &s->doc->elements[s->selected_idx];
  int hx[HANDLE_COUNT], hy[HANDLE_COUNT];
  get_handle_rects(s, el, hx, hy);
  for (int i = 0; i < HANDLE_COUNT; i++) {
    int hit_x = hx[i] - HANDLE_HIT_OUTSET;
    int hit_y = hy[i] - HANDLE_HIT_OUTSET;
    int hit_size = HANDLE_SIZE + HANDLE_HIT_OUTSET * 2;
    if (lx >= hit_x && lx < hit_x + hit_size &&
        ly >= hit_y && ly < hit_y + hit_size)
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
    case evMouseMove:
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
  s->drag = (drag_state_t){.mode = DRAG_NONE};
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
  bool was_placing = s && s->drag.mode == DRAG_RUBBERBND;
  canvas_reset_drag(s);
  if (was_placing)
    canvas_set_select_tool();
}

static void canvas_update_preview(canvas_state_t *s, int type, irect16_t form_rc,
                                  const char *text, uint32_t flags) {
  form_doc_t *doc;
  if (!s || !s->doc || !s->doc->canvas_win) return;
  doc = s->doc;
  if (type < 0 || !ctrl_type_to_proc(type)) return;
  irect16_t canvas_rc = form_to_canvas_rect(s, form_rc);

  if (!canvas_child_window_alive(doc->canvas_win, s->preview_win) ||
      s->preview_type != type) {
    canvas_destroy_preview(s);
    s->preview_type = type;
    s->preview_win = create_window(text ? text : "", flags,
                                   MAKERECT(0, 0, MAX(form_rc.w, 1), MAX(form_rc.h, 1)),
                                   doc->canvas_win, preview_ctrl_proc, 0, NULL);
    if (!s->preview_win) return;
    s->preview_win->notabstop = true;
  }

  move_window(s->preview_win, canvas_rc.x, canvas_rc.y);
  resize_window(s->preview_win, MAX(form_rc.w, 1), MAX(form_rc.h, 1));
  if (text && strcmp(s->preview_win->title, text) != 0) {
    snprintf(s->preview_win->title, sizeof(s->preview_win->title), "%s", text);
    invalidate_window(s->preview_win);
  }
}

static void canvas_sync_live_element_window(form_doc_t *doc, form_element_t *el) {
  canvas_state_t *s;
  if (!doc || !doc->canvas_win || !el ||
      !canvas_child_window_alive(doc->canvas_win, el->live_win))
    return;
  s = (canvas_state_t *)doc->canvas_win->userdata;
  if (!s) return;
  irect16_t r = form_to_canvas_rect(s, el->frame);
  move_window(el->live_win, r.x, r.y);
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
  irect16_t r = form_to_canvas_rect(s, el->frame);
  el->live_win = create_window(el->text, el->flags,
                               MAKERECT(r.x, r.y, r.w, r.h),
                               doc->canvas_win, design_live_ctrl_proc, 0, el);
  if (!el->live_win) return;
  el->live_win->id = el->id;
  el->live_win->notabstop = true;
}

void canvas_sync_live_controls(form_doc_t *doc) {
  if (!doc || !doc->canvas_win) return;
  for (int i = 0; i < doc->element_count; i++)
    canvas_sync_live_element_window(doc, &doc->elements[i]);
  invalidate_window(doc->canvas_win);
  property_browser_refresh(doc);
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
  irect16_t r = form_to_canvas_rect(s, el->frame);
  draw_sel_rect(R(r.x - 1, r.y - 1, r.w + 2, r.h + 2));

  // Solid handle squares
  uint32_t hcol = 0xFFFFFFFF;
  for (int i = 0; i < HANDLE_COUNT; i++)
    fill_rect(hcol, R(hx[i], hy[i], HANDLE_SIZE, HANDLE_SIZE));
}

// Draw a rubber-band rectangle (for placement drag) in form coords.
static void draw_rubber_band(window_t *win, canvas_state_t *s) {
  (void)win;
  if (s->drag.mode != DRAG_RUBBERBND) return;
  irect16_t rb = s->drag.place.band;
  int x0 = rb.x < 0 ? 0 : rb.x;
  int y0 = rb.y < 0 ? 0 : rb.y;
  int x1 = x0 + rb.w;
  int y1 = y0 + rb.h;
  if (x1 > s->doc->form_size.w) x1 = s->doc->form_size.w;
  if (y1 > s->doc->form_size.h) y1 = s->doc->form_size.h;
  if (x1 <= x0 || y1 <= y0) return;
  draw_sel_rect(form_to_canvas_rect(s, R(x0, y0, x1 - x0, y1 - y0)));
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
static void draw_grid(canvas_state_t *s, irect16_t canvas_rc) {
  form_doc_t *doc = s->doc;
  if (!doc->show_grid) return;
  int grid = doc->grid_size;
  if (grid <= 1) return;  // grid=1 would paint every pixel; skip for performance
  uint32_t tex = ensure_grid_dot_texture(grid);
  if (tex == 0) return;
  draw_sprite_region((int)tex, canvas_rc,
                     UV_RECT(0.0f, 0.0f,
                             (float)canvas_rc.w / (float)grid,
                             (float)canvas_rc.h / (float)grid),
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

static void ctrl_make_caption(int type, int index, char *text, size_t text_sz) {
  if (!text || text_sz == 0) return;
  snprintf(text, text_sz, "%s%d", ctrl_type_name(type), index);
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
  ctrl_make_caption(type, n, el->text, sizeof(el->text));
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
  canvas_sync_live_controls(doc);
  doc->modified = true;
  form_doc_update_title(doc);
  return index;
}

static irect16_t canvas_rubber_band_rect(canvas_state_t *s, canvas_pt_t pos) {
  form_doc_t *doc = s->doc;
  form_pt_t p = canvas_to_form_pt(s, pos);
  form_pt_t o = canvas_to_form_pt(s, s->drag.place.start);
  int x0 = snap(doc, o.x < p.x ? o.x : p.x);
  int y0 = snap(doc, o.y < p.y ? o.y : p.y);
  int x1 = snap(doc, o.x < p.x ? p.x : o.x);
  int y1 = snap(doc, o.y < p.y ? p.y : o.y);
  return (irect16_t){x0, y0, x1 - x0, y1 - y0};
}

static void canvas_update_rubber_band(canvas_state_t *s, canvas_pt_t pos) {
  s->drag.place.band = canvas_rubber_band_rect(s, pos);
}

static void canvas_update_placement_preview(canvas_state_t *s) {
  form_doc_t *doc = s->doc;
  int ctrl_type = s->drag.place.ctrl_type;
  char preview_text[64];
  if (ctrl_type < 0) return;
  irect16_t preview = s->drag.place.band;
  if (preview.w < MIN_ELEM_W || preview.h < MIN_ELEM_H) {
    isize16_t size = default_ctrl_size(ctrl_type);
    preview.w = size.w;
    preview.h = size.h;
  }
  ctrl_make_caption(ctrl_type, doc->type_counters[ctrl_type] + 1,
                    preview_text, sizeof(preview_text));
  canvas_update_preview(s, ctrl_type, preview, preview_text, 0);
}

// ============================================================
// Apply resize delta to the selected element
// ============================================================
typedef struct {
  int left, top, right, bottom;
} handle_edges_t;

static const handle_edges_t k_handle_edges[HANDLE_COUNT] = {
  [HANDLE_TL] = {1, 1, 0, 0},
  [HANDLE_TC] = {0, 1, 0, 0},
  [HANDLE_TR] = {0, 1, 1, 0},
  [HANDLE_ML] = {1, 0, 0, 0},
  [HANDLE_MR] = {0, 0, 1, 0},
  [HANDLE_BL] = {1, 0, 0, 1},
  [HANDLE_BC] = {0, 0, 0, 1},
  [HANDLE_BR] = {0, 0, 1, 1},
};

static void canvas_apply_resize(canvas_state_t *s, int dx, int dy) {
  form_doc_t     *doc = s->doc;
  form_element_t *el  = &doc->elements[s->selected_idx];
  irect16_t start = s->drag.resize.frame;
  int handle = s->drag.resize.handle;
  if (handle < 0 || handle >= HANDLE_COUNT) return;
  handle_edges_t edges = k_handle_edges[handle];
  int left = start.x;
  int top = start.y;
  int right = start.x + start.w;
  int bottom = start.y + start.h;

  if (edges.left) left += dx;
  if (edges.top) top += dy;
  if (edges.right) right += dx;
  if (edges.bottom) bottom += dy;

  if (doc->snap_to_grid && doc->grid_size > 1) {
    int g = doc->grid_size;
    if (edges.left) left = snap_val(left, g);
    if (edges.top) top = snap_val(top, g);
    if (edges.right) right = snap_val(right, g);
    if (edges.bottom) bottom = snap_val(bottom, g);
  }

  int x = left;
  int y = top;
  int w = right - left;
  int h = bottom - top;

  if (w < MIN_ELEM_W) {
    if (edges.left) x = start.x + start.w - MIN_ELEM_W;
    w = MIN_ELEM_W;
  }
  if (h < MIN_ELEM_H) {
    if (edges.top) y = start.y + start.h - MIN_ELEM_H;
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
      st->selected_idx = -1;
      st->pan          = (ipoint16_t){0, 0};
      st->drag         = (drag_state_t){.mode = DRAG_NONE};
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
      irect16_t form_rc = form_to_canvas_rect(s, R(0, 0, doc->form_size.w, doc->form_size.h));
      fill_rect(get_sys_color(brWindowBg), form_rc);
      fill_rect(get_sys_color(brDarkEdge), R(form_rc.x - 1, form_rc.y - 1, form_rc.w + 2, 1));
      fill_rect(get_sys_color(brDarkEdge), R(form_rc.x - 1, form_rc.y - 1, 1, form_rc.h + 2));
      fill_rect(get_sys_color(brDarkEdge), R(form_rc.x - 1, form_rc.y + form_rc.h, form_rc.w + 2, 1));
      fill_rect(get_sys_color(brDarkEdge), R(form_rc.x + form_rc.w, form_rc.y - 1, 1, form_rc.h + 2));

      // Dot grid on the form surface
      draw_grid(s, form_rc);

      for (window_t *child = win->children; child; child = child->next)
        send_message(child, evPaint, 0, NULL);
      canvas_set_draw_space(win);
      draw_handles(win, s);
      draw_rubber_band(win, s);

      return true;
    }

    case evParentNotify: {
      if (!s || !doc || !lparam) return false;
      parent_notify_t *pn = (parent_notify_t *)lparam;
      if (!pn->child || pn->child->parent != win)
        return false;

      uint32_t child_msg = pn->child_msg;
      uint32_t child_wp = pn->child_wparam;
      switch (child_msg) {
        case evLeftButtonDown:
        case evLeftButtonDoubleClick:
        case evLeftButtonUp:
        case evRightButtonDown:
        case evRightButtonUp:
        case evMouseMove: {
          int lx = (int16_t)LOWORD(child_wp);
          int ly = (int16_t)HIWORD(child_wp);
          uint32_t parent_wp = MAKEDWORD(pn->child->frame.x + lx,
                                         pn->child->frame.y + ly);
          return win_canvas_proc(win, child_msg, parent_wp, pn->child_lparam);
        }
        case evWheel:
        case evKeyDown:
        case evKeyUp:
        case evTextInput:
          return win_canvas_proc(win, child_msg, child_wp, pn->child_lparam);
        default:
          return false;
      }
    }

    case evLeftButtonDown: {
      if (!s || !doc) return false;
      form_doc_activate(doc);
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      int tool = g_app ? g_app->current_tool : ID_TOOL_SELECT;

      if (s->drag.mode != DRAG_NONE) {
        canvas_cancel_drag(s);
        canvas_sync_live_controls(doc);
        return true;
      }

      if (tool == ID_TOOL_SELECT) {
        // Check resize handles first
        int handle = hit_test_handles(s, lx, ly);
        if (handle >= 0 && s->selected_idx >= 0) {
          form_element_t *el = &doc->elements[s->selected_idx];
          s->drag = (drag_state_t){
            .mode = DRAG_RESIZE,
            .resize = {
              .start = {lx, ly},
              .frame = el->frame,
              .handle = handle,
            },
          };
          set_capture(win);
          return true;
        }
        // Hit test elements
        int hit = hit_test_elements(s, lx, ly);
        s->selected_idx = hit;
        property_browser_refresh(doc);
        if (hit >= 0) {
          form_element_t *el = &doc->elements[hit];
          s->drag = (drag_state_t){
            .mode = DRAG_MOVE,
            .move = {
              .start = {lx, ly},
              .frame = el->frame,
            },
          };
          set_capture(win);
        } else {
          s->drag = (drag_state_t){.mode = DRAG_NONE};
        }
        invalidate_window(win);
        return true;
      }

      // Placement tools: start rubber-band drag
      int ctrl_type = tool_to_ctrl_type(tool);
      if (ctrl_type >= 0) {
        form_pt_t fp = canvas_to_form_pt(s, (canvas_pt_t){lx, ly});
        char preview_text[64];
        // Clamp to form surface
        if (fp.x < 0) fp.x = 0;
        if (fp.y < 0) fp.y = 0;
        if (fp.x > doc->form_size.w) fp.x = doc->form_size.w;
        if (fp.y > doc->form_size.h) fp.y = doc->form_size.h;
        s->drag = (drag_state_t){
          .mode = DRAG_RUBBERBND,
          .place = {
            .start = {lx, ly},
            .band = {fp.x, fp.y, 0, 0},
            .ctrl_type = ctrl_type,
          },
        };
        s->selected_idx = -1;
        property_browser_refresh(doc);
        ctrl_make_caption(ctrl_type, doc->type_counters[ctrl_type] + 1,
                          preview_text, sizeof(preview_text));
        canvas_update_preview(s, ctrl_type, R(fp.x, fp.y, 1, 1), preview_text, 0);
        set_capture(win);
        return true;
      }
      return false;
    }

    case evMouseMove: {
      if (!s) return false;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);

      if (s->drag.mode == DRAG_MOVE && s->selected_idx >= 0) {
        form_element_t *el = &doc->elements[s->selected_idx];
        int nx = snap(doc, s->drag.move.frame.x + (lx - s->drag.move.start.x));
        int ny = snap(doc, s->drag.move.frame.y + (ly - s->drag.move.start.y));
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

      if (s->drag.mode == DRAG_RESIZE && s->selected_idx >= 0) {
        int dx = lx - s->drag.resize.start.x;
        int dy = ly - s->drag.resize.start.y;
        canvas_apply_resize(s, dx, dy);
        doc->modified = true;
        canvas_sync_live_controls(doc);
        invalidate_window(win);
        return true;
      }

      if (s->drag.mode == DRAG_RUBBERBND) {
        canvas_update_rubber_band(s, (canvas_pt_t){lx, ly});
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

      if (s->drag.mode == DRAG_MOVE && s->selected_idx >= 0) {
        doc->modified = true;
        form_doc_update_title(doc);
      }

      if (s->drag.mode == DRAG_RESIZE) {
        doc->modified = true;
        form_doc_update_title(doc);
      }

      if (s->drag.mode == DRAG_RUBBERBND) {
        int ctrl_type = s->drag.place.ctrl_type;
        if (ctrl_type >= 0) {
          irect16_t frame = canvas_rubber_band_rect(s, (canvas_pt_t){lx, ly});
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
