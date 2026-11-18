#include "comic_panel.h"
#include "comic_stage.h"
#include <orion/commctl/commctl_internal.h>
#include <math.h>

typedef struct {
  fog_comic_panel_t panel;
  uint32_t page_texture;
  int page_w, page_h;
  uint32_t layer_texture;          // the reader's wash, rendered from panel.layer
  const comic_layer_t *layer_source;
  uint32_t layer_revision;
  bool drawing;
  bool modified;
  int pigment;
  comic_point_t last;
} fog_art_state_t;

// Where each shot's artwork lies: a cell of a 3 x 2 sheet, or the whole harbor plate. `focus` keeps
// the top of a portrait shot in view when the panel is wider than the cell.
typedef struct { uint8_t sheet, cell; float focus; } fog_shot_art_t;

static const fog_shot_art_t fog_original_shots[] = {
  [FOG_SHOT_BOAST]   = {FOG_SHEET_INSERTS, 0, 0},   [FOG_SHOT_DOCK]    = {FOG_SHEET_PANELS, 0, .5f},
  [FOG_SHOT_LESSON]  = {FOG_SHEET_PANELS, 1, 0},    [FOG_SHOT_KNOTS]   = {FOG_SHEET_INSERTS, 1, .5f},
  [FOG_SHOT_HUT]     = {FOG_SHEET_INSERTS, 2, .5f}, [FOG_SHOT_POINT]   = {FOG_SHEET_PANELS, 2, .5f},
  [FOG_SHOT_LANTERN] = {FOG_SHEET_INSERTS, 3, 0},   [FOG_SHOT_HARBOR]  = {FOG_SHEET_HARBOR, 0, .5f},
  [FOG_SHOT_ROWING]  = {FOG_SHEET_INSERTS, 5, 0},   [FOG_SHOT_SAIL]    = {FOG_SHEET_PANELS, 4, .5f},
};

#define FOG_SHEET_FILE(key, name) [FOG_SHEET_##key] = "orion/" name ".art",
static const char *const fog_sheet_files[] = {
  [FOG_SHEET_PANELS]  = "orion/panels.art",
  [FOG_SHEET_INSERTS] = "orion/inserts.art",
  [FOG_SHEET_HARBOR]  = "orion/harbor.art",
  FOG_STORY_SHEETS(FOG_SHEET_FILE)
};
#undef FOG_SHEET_FILE

static fog_shot_art_t fog_shot_art(fog_shot_t shot) {
  if (shot < FOG_SHOT_MOORINGS_1) return fog_original_shots[shot];
  int index = shot - FOG_SHOT_MOORINGS_1;
  return (fog_shot_art_t){FOG_SHEET_MOORINGS + index / FOG_SHEET_CELLS, index % FOG_SHEET_CELLS, .5f};
}

static uint32_t fog_load_art(const comic_archive_t *archive, const char *name, frect_t *uv) {
  int w, h;
  uint8_t *pixels = comic_archive_read_art(archive, name, &w, &h);
  if (!pixels) return 0;
  if (uv) {
    int left = w, right = 0, top = h, bottom = 0;
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        if (pixels[((size_t)y * w + x) * 4 + 3] < 24) continue;
        left = MIN(left, x); right = MAX(right, x + 1);
        top = MIN(top, y); bottom = MAX(bottom, y + 1);
      }
    }
    if (left < right && top < bottom)
      *uv = (frect_t){(float)left / w, (float)top / h, (float)right / w, (float)bottom / h};
  }
  uint32_t tex = R_CreateTextureSRGBA8(w, h, pixels, R_FILTER_LINEAR, R_WRAP_CLAMP);
  image_free(pixels);
  return tex;
}

// The shot's cell on its sheet, as UV corners (x0, y0, x1, y1); a hairline inset hides the gutters.
static frect_t fog_cell_uv(fog_shot_t shot) {
  fog_shot_art_t art = fog_shot_art(shot);
  if (art.sheet == FOG_SHEET_HARBOR) return (frect_t){0, 0, 1, 1};
  float u = (float)(art.cell % 3) / 3, v = (float)(art.cell / 3) / 2;
  return (frect_t){u + .002f, v + .003f, u + .331f, v + .497f};
}

// The part of the cell (0..1 both ways) a panel of r's shape shows: the cell cropped to r's aspect.
static frect_t fog_shot_crop(fog_shot_t shot, irect16_t r) {
  fog_shot_art_t art = fog_shot_art(shot);
  float aspect = art.sheet == FOG_SHEET_HARBOR ? 1.5f : 1;
  float ratio = (float)r.w / MAX(1, r.h) / aspect;
  float x = ratio < 1 ? .5f * (1 - ratio) : 0;
  float y = ratio > 1 ? 1 - 1 / ratio : 0;
  return (frect_t){x, y * art.focus, 1 - x, 1 - y * (1 - art.focus)};
}

static void fog_draw_watercolor(const fog_comic_art_t *art, fog_shot_t shot, irect16_t r) {
  uint32_t texture = art->sheets[fog_shot_art(shot).sheet];
  if (!texture) return;
  frect_t cell = fog_cell_uv(shot), crop = fog_shot_crop(shot, r);
  float w = cell.w - cell.x, h = cell.h - cell.y;
  draw_sprite_region((int)texture, r, UV_RECT(cell.x + crop.x * w, cell.y + crop.y * h,
                     cell.x + crop.w * w, cell.y + crop.h * h), 0xFFFFFFFF, NO_ALPHA);
}

static bool fog_bubble_at_bottom(const fog_comic_panel_t *panel) {
  if (panel->bubble.edge == COMIC_BUBBLE_DEFAULT) return panel->shot != FOG_SHOT_HARBOR;
  return panel->bubble.edge == COMIC_BUBBLE_BOTTOM;
}

static void fog_arrange_bubble(window_t *bubble, irect16_t area, int anchor,
                               const fog_comic_panel_t *panel) {
  if (!bubble || !window_has_state(bubble, WINDOW_STATE_VISIBLE)) return;
  bool at_bottom = fog_bubble_at_bottom(panel);
  send_message(bubble, bbSetTailSide, at_bottom ? TOOLTIP_TAIL_TOP : TOOLTIP_TAIL_BOTTOM, NULL);
  layout_measure_t measure = layout_measure_child(bubble, area.w, area.h);
  int w = MIN(area.w, measure.desired_w), h = MIN(area.h, measure.desired_h);
  int x = MAX(area.x, MIN(anchor - w / 2, area.x + area.w - w));
  int travel = (area.h - h) / 2;
  if (panel->shot == FOG_SHOT_HARBOR) travel = MAX(0, area.h / 2 - h);
  int inset = travel * panel->bubble.inset / 100;
  int y = at_bottom ? area.y + area.h - h - inset : area.y + inset;
  send_message(bubble, bbSetTailOffset, MAX(1, MIN(w, anchor - x)), NULL);
  layout_arrange_child(bubble, R(x, y, w, h));
}

static void fog_arrange_speech(window_t *win, const fog_comic_panel_t *panel) {
  const char *style = ui_selector(win, COMIC_PANEL_CLASS);
  irect16_t area = rect_inset(get_client_rect(win), ui_px(style, "--speech-inset"));
  if (area.w <= 0 || area.h <= 0) return;
  bool at_bottom = fog_bubble_at_bottom(panel);
  int controls = ui_px(style, "--reader-overlay-inset") + ui_px(style, "--reader-control-size");
  // A bubble along the top of a page starts below the row of reader controls (shelf, palette).
  if (!at_bottom && panel->at_page_top)
    area = rect_trim_top(area, MIN(controls, area.h / 4));
  if (at_bottom && panel->at_page_bottom) {
    // The dock scene has faces low in the panel. Leave room beside its bubble for the reader controls.
    if (panel->shot == FOG_SHOT_DOCK)
      area = rect_trim_right(area, MIN(controls, area.w / 3));
    else
      area = rect_trim_bottom(area, MIN(controls, area.h / 3));
  }
  int anchor = 50;
  switch (panel->shot) {
    case FOG_SHOT_LESSON:  anchor = 33; break;
    case FOG_SHOT_DOCK:    anchor = 66; break;
    case FOG_SHOT_POINT:   anchor = 72; break;
    case FOG_SHOT_LANTERN: anchor = 60; break;
    case FOG_SHOT_SAIL:    anchor = 78; break;
    case FOG_SHOT_HARBOR:  anchor = 30; break;
    default: break;
  }
  if (panel->bubble.x >= 0) anchor = panel->bubble.x;
  fog_arrange_bubble(panel->speech, area, area.x + area.w * anchor / 100, panel);
}

// Three rowboats moored along the bottom of the harbor plate.
static void fog_paint_boats(irect16_t r, const fog_comic_art_t *art) {
  irect16_t strip = rect_trim_top(rect_inset(r, 10), r.h / 2);
  for (int i = 0; i < 3; i++) {
    irect16_t slot = rect_trim_right(rect_trim_left(strip, i * strip.w / 3), strip.w - (i + 1) * strip.w / 3);
    int width = MAX(0, MIN(slot.w * 9 / 10, slot.h * 4 / 3));
    irect16_t boat = rect_center(rect_trim_top(slot, slot.h / 4), width, width * 4 / 9);
    if (boat.w > 0 && boat.h > 0) draw_sprite_region((int)art->boat, boat, &art->boat_uv, 0xFFFFFFFF, 0);
  }
}

// The reader's colours, then the ink over them, both through the panel's crop of the cell.
static void fog_paint_line_art(fog_art_state_t *st, irect16_t r) {
  const fog_comic_panel_t *panel = &st->panel;
  if (!st->layer_texture || st->layer_source != panel->layer || st->layer_revision != panel->layer->revision) {
    uint8_t *rgba = malloc((size_t)COMIC_LAYER_PIXELS * 4);
    if (!rgba) return;
    comic_layer_render(panel->layer, rgba);
    if (st->layer_texture) R_DeleteTexture(st->layer_texture);
    st->layer_texture = R_CreateTextureSRGBA8(COMIC_LAYER_SIZE, COMIC_LAYER_SIZE, rgba, R_FILTER_LINEAR, R_WRAP_CLAMP);
    st->layer_source = panel->layer;
    st->layer_revision = panel->layer->revision;
    free(rgba);
  }
  frect_t crop = fog_shot_crop(panel->shot, r);
  if (st->layer_texture) draw_sprite_region((int)st->layer_texture, r, &crop, 0xFFFFFFFF, NO_ALPHA);
  uint32_t ink = panel->art->lineart[panel->shot];
  if (ink) draw_sprite_region((int)ink, r, &crop, 0xFFFFFFFF, 0);
}

static void fog_paint_panel(window_t *win, fog_art_state_t *st) {
  const fog_comic_panel_t *panel = &st->panel;
  const irect16_t frame = get_client_rect(win);
  if (frame.w < 24 || frame.h < 24 || panel->shot == FOG_SHOT_NONE) return;
  if (panel->shot == FOG_SHOT_ARCHIVE) {
    if (st->page_texture) {
      int w = MIN(frame.w, (int)((int64_t)frame.h * st->page_w / st->page_h));
      int h = MIN(frame.h, (int)((int64_t)frame.w * st->page_h / st->page_w));
      draw_rect(st->page_texture, rect_center(frame, w, h));
    } else {
      theme_draw_text_wrapped(ui_selector(win, "Label"), ui_tr("This page could not be opened."),
                              rect_inset(frame, 24), get_sys_color(brTextError), CTRL_NORMAL);
    }
    return;
  }
  irect16_t r = rect_inset(frame, 1);
  fill_rect(ui_color(ui_selector(win, COMIC_PANEL_CLASS), "border-color"), frame);
  if (!panel->art) return;
  if (panel->layer) fog_paint_line_art(st, r);
  else fog_draw_watercolor(panel->art, panel->shot, r);
  if (panel->shot == FOG_SHOT_HARBOR && !panel->layer) fog_paint_boats(r, panel->art);
}

bool fog_art_load(fog_comic_art_t *art, const comic_archive_t *archive, const comic_story_t *story) {
  for (int page = 0; page < story->count; page++) {
    for (int i = 0; i < ARRAY_LEN(story->pages[page]); i++) {
      const comic_beat_t *beat = &story->pages[page][i];
      if (!beat->shot || beat->shot == FOG_SHOT_ARCHIVE || beat->layer >= 0) continue;
      int sheet = fog_shot_art(beat->shot).sheet;
      if (!art->sheets[sheet] && !(art->sheets[sheet] = fog_load_art(archive, fog_sheet_files[sheet], NULL)))
        return false;
    }
  }
  if (art->sheets[FOG_SHEET_HARBOR]) {
    art->boat_uv = (frect_t){0, 0, 1, 1};
    if (!(art->boat = fog_load_art(archive, "orion/rowboat.art", &art->boat_uv))) return false;
  }
  for (int i = 0; i < story->layer_count; i++) {
    char name[64];
    fog_shot_t shot = story->layers[i];
    snprintf(name, sizeof(name), "orion/lineart/%s.art", comic_shot_name(shot));
    if (!(art->lineart[shot] = fog_load_art(archive, name, NULL))) return false;
  }
  return true;
}

void fog_art_clear(fog_comic_art_t *art) {
  if (art->boat) R_DeleteTexture(art->boat);
  for (int i = 0; i < ARRAY_LEN(art->sheets); i++)
    if (art->sheets[i]) R_DeleteTexture(art->sheets[i]);
  for (int i = 0; i < ARRAY_LEN(art->lineart); i++)
    if (art->lineart[i]) R_DeleteTexture(art->lineart[i]);
  memset(art, 0, sizeof(*art));
}

// A point in the panel, in pixels of the layer under it.
static comic_point_t fog_layer_point(window_t *win, const fog_comic_panel_t *panel, ipoint16_t p) {
  irect16_t r = rect_inset(get_client_rect(win), 1);
  frect_t crop = fog_shot_crop(panel->shot, r);
  float u = crop.x + (crop.w - crop.x) * (p.x - r.x) / MAX(1, r.w);
  float v = crop.y + (crop.h - crop.y) * (p.y - r.y) / MAX(1, r.h);
  return (comic_point_t){u * COMIC_LAYER_SIZE, v * COMIC_LAYER_SIZE};
}

static void comic_panel_release(window_t *win, fog_art_state_t *st) {
  bool notify = st->drawing && st->modified;
  st->drawing = st->modified = false;
  set_capture(NULL);
  if (notify) send_message(get_root_window(win), evCommand, MAKEDWORD(win->id, COMIC_PAINT_FINISHED), win);
}

result_t comic_panel_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  fog_art_state_t *st = win->userdata;
  switch (msg) {
    case evCreate:
      return allocate_window_data(win, sizeof(*st)) != NULL;
    case evPaint:
      if (st) fog_paint_panel(win, st);
      return false;
    case evDestroy:
      if (st && st->page_texture) R_DeleteTexture(st->page_texture);
      if (st && st->layer_texture) R_DeleteTexture(st->layer_texture);
      return true;
    case evResize:
      if (st) fog_arrange_speech(win, &st->panel);
      return true;
    case COMIC_SET_PANEL: {
      const fog_comic_panel_t *next = lparam;
      if (!st || !next) return false;
      if (next->archive != st->panel.archive || next->archive_page != st->panel.archive_page) {
        if (st->page_texture) R_DeleteTexture(st->page_texture);
        st->page_texture = 0;
        if (next->archive) {
          uint8_t *rgba = comic_archive_read_page(next->archive, next->archive_page, &st->page_w, &st->page_h);
          if (rgba) {
            st->page_texture = R_CreateTextureSRGBA8(st->page_w, st->page_h, rgba, R_FILTER_LINEAR, R_WRAP_CLAMP);
            image_free(rgba);
          }
        }
      }
      st->panel = *next;
      fog_arrange_speech(win, &st->panel);
      invalidate_window(win);
      return true;
    }
    case evLeftButtonDown:
    case evMouseMove:
    case evLeftButtonUp: {
      if (!st || !st->panel.layer || comic_stage_turning(st->panel.stage)) return false;
      comic_point_t point = fog_layer_point(win, &st->panel,
                                            (ipoint16_t){(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)});
      if (msg == evLeftButtonDown) {
        set_capture(win);
        st->drawing = true;
        st->pigment = st->panel.pigment;
        st->last = point;
      } else if (!st->drawing) {
        return false;
      }
      if (comic_layer_stroke(st->panel.layer, st->last, point, st->pigment)) {
        st->modified = true;
        invalidate_window(win);
      }
      st->last = point;
      if (msg == evLeftButtonUp) comic_panel_release(win, st);
      return true;
    }
    case evPointerCancel:
      if (st && st->drawing) comic_panel_release(win, st);
      return true;
    default: return false;
  }
}
