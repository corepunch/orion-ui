// Tool palette for the form editor.
// Defaults to a reportview icon+name list, similar to later Visual Basic
// toolbox versions. Define FE_TOOLBOX_USE_BUTTON_GRID to use the classic
// compact button grid.
// toolbox.png is a generated 16x16-px-per-tile shared component icon atlas.

#include "formeditor.h"
#include "../../commctl/commctl.h"
#include "../../commctl/columnview.h"
#include "../../kernel/renderer.h"
#include "../../user/image.h"

#ifndef FE_TOOLBOX_USE_BUTTON_GRID
typedef struct {
  bitmap_strip_t strip;
  uint32_t strip_tex;
} tool_palette_state_t;

static reportview_item_t g_tools[FE_MAX_COMPONENTS + 1];
#else
static toolbox_item_t g_tools[FE_MAX_COMPONENTS + 1];
#endif
static int g_tool_count = 0;

static int palette_win_y(void) {
  return MENUBAR_HEIGHT + 4;
}

static int palette_item_count(void) {
  int items = 1;  // Select tool
  for (int i = 0; i < fe_component_count(); i++) {
    const fe_component_desc_t *c = fe_component_at(i);
    if (!c) continue;
    if ((c->capabilities & (FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBOX)) ==
        (FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBOX))
      items++;
  }
  return items;
}

static int palette_win_h(void) {
#ifdef FE_TOOLBOX_USE_BUTTON_GRID
  int rows = (palette_item_count() + TOOLBOX_COLS - 1) / TOOLBOX_COLS;
  return TITLEBAR_HEIGHT + rows * FE_TOOLBOX_BTN_SIZE + 4;
#else
  int visible_items = palette_item_count();
  if (visible_items < 8) visible_items = 8;
  if (visible_items > 18) visible_items = 18;
  return TITLEBAR_HEIGHT + visible_items * COLUMNVIEW_ENTRY_HEIGHT + 4;
#endif
}

static window_t *create_tool_palette(hinstance_t hinstance) {
  window_t *tp = create_window(
      "Tools",
      WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(PALETTE_WIN_X, palette_win_y(), PALETTE_WIN_W, palette_win_h()),
      NULL, win_tool_palette_proc, hinstance, NULL);
  if (tp) show_window(tp, true);
  return tp;
}

void formeditor_rebuild_tool_palette(void) {
  if (!g_app) return;
  if (g_app->tool_win) {
    destroy_window(g_app->tool_win);
    g_app->tool_win = NULL;
  }
  g_app->current_tool = ID_TOOL_SELECT;
  g_app->tool_win = create_tool_palette(g_app->hinstance);
}

static void build_tool_items(void) {
  g_tool_count = 0;
#ifdef FE_TOOLBOX_USE_BUTTON_GRID
  g_tools[g_tool_count++] = (toolbox_item_t){
      ID_TOOL_SELECT,
      toolbox_icon_cursor,
      "Select"
  };
#else
  g_tools[g_tool_count++] = (reportview_item_t){
      .text = "Select",
      .icon = toolbox_icon_cursor,
      .color = get_sys_color(brTextNormal),
      .userdata = ID_TOOL_SELECT,
  };
#endif
  for (int i = 0; i < fe_component_count() && g_tool_count < FE_MAX_COMPONENTS + 1; i++) {
    const fe_component_desc_t *c = fe_component_at(i);
    if (!c) continue;
    if ((c->capabilities & (FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBOX)) !=
        (FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBOX))
      continue;
#ifdef FE_TOOLBOX_USE_BUTTON_GRID
    g_tools[g_tool_count++] = (toolbox_item_t){
        c->toolbox_ident,
        c->toolbox_icon,
        c->display_name
    };
#else
    g_tools[g_tool_count++] = (reportview_item_t){
        .text = c->display_name,
        .icon = c->toolbox_icon,
        .color = get_sys_color(brTextNormal),
        .userdata = (uint32_t)c->toolbox_ident,
    };
#endif
  }
}

#ifndef FE_TOOLBOX_USE_BUTTON_GRID
#ifdef SHAREDIR
static bool load_toolbox_strip(tool_palette_state_t *st, const char *path) {
  if (!st || !path || !g_ui_runtime.running)
    return false;

  int w = 0;
  int h = 0;
  uint8_t *pixels = load_image(path, &w, &h);
  if (!pixels)
    return false;
  if (w < FE_TOOLBOX_ICON_W || h < FE_TOOLBOX_ICON_W ||
      (w % FE_TOOLBOX_ICON_W) != 0 || (h % FE_TOOLBOX_ICON_W) != 0) {
    image_free(pixels);
    return false;
  }

  uint32_t tex = R_CreateTextureRGBA(w, h, pixels, R_FILTER_NEAREST, R_WRAP_CLAMP);
  image_free(pixels);
  if (!tex)
    return false;

  if (st->strip_tex)
    R_DeleteTexture(st->strip_tex);
  st->strip_tex = tex;
  st->strip = (bitmap_strip_t){
      .tex = tex,
      .icon_w = FE_TOOLBOX_ICON_W,
      .icon_h = FE_TOOLBOX_ICON_W,
      .cols = w / FE_TOOLBOX_ICON_W,
      .sheet_w = w,
      .sheet_h = h,
  };
  return true;
}
#endif

#endif

static void select_tool_by_ident(window_t *win, int ident) {
  if (g_app) {
    g_app->current_tool = ident;
    if (g_app->doc && g_app->doc->canvas_win)
      invalidate_window(g_app->doc->canvas_win);
    if (g_app->menubar_win)
      send_message(g_app->menubar_win, evCommand,
                   MAKEDWORD((uint16_t)ident, btnClicked),
                   win);
    else
      handle_menu_command((uint16_t)ident);
  }
}

#ifndef FE_TOOLBOX_USE_BUTTON_GRID
static void populate_tool_list(window_t *win) {
  if (!win)
    return;
  build_tool_items();
  send_message(win, RVM_SETREDRAW, 0, NULL);
  send_message(win, RVM_CLEAR, 0, NULL);
  for (int i = 0; i < g_tool_count; i++)
    send_message(win, RVM_ADDITEM, 0, &g_tools[i]);
  send_message(win, RVM_SETREDRAW, 1, NULL);

  int current = g_app ? g_app->current_tool : ID_TOOL_SELECT;
  for (int i = 0; i < g_tool_count; i++) {
    if ((int)g_tools[i].userdata == current) {
      send_message(win, RVM_SETSELECTION, (uint32_t)i, NULL);
      break;
    }
  }
}
#endif

result_t win_tool_palette_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
#ifdef FE_TOOLBOX_USE_BUTTON_GRID
      win_toolbox(win, msg, wparam, lparam);
      send_message(win, bxSetButtonSize, FE_TOOLBOX_BTN_SIZE, NULL);

#ifdef SHAREDIR
      char path[4096];
      snprintf(path, sizeof(path), "%s/" SHAREDIR "/toolbox.png", ui_get_exe_dir());
      send_message(win, bxLoadStrip, FE_TOOLBOX_ICON_W, path);
#endif

      build_tool_items();
      send_message(win, bxSetItems, g_tool_count, (void *)g_tools);
      send_message(win, bxSetActiveItem, ID_TOOL_SELECT, NULL);
      return true;
#else
      win_reportview(win, msg, wparam, lparam);
      tool_palette_state_t *st = allocate_window_data(win, sizeof(tool_palette_state_t));

#ifdef SHAREDIR
      char path[4096];
      snprintf(path, sizeof(path), "%s/" SHAREDIR "/toolbox.png", ui_get_exe_dir());
      if (load_toolbox_strip(st, path))
        send_message(win, RVM_SETICONSTRIP, 0, &st->strip);
#else
      (void)st;
#endif

      send_message(win, RVM_SETVIEWMODE, RVM_VIEW_ICON, NULL);
      send_message(win, RVM_SETCOLUMNWIDTH, PALETTE_WIN_W - 12, NULL);
      send_message(win, RVM_SETPRESERVEICONCOLORS, 1, NULL);
      send_message(win, RVM_SETICONTEXTGAP, 4, NULL);
      populate_tool_list(win);
      return true;
#endif
    }

#ifndef FE_TOOLBOX_USE_BUTTON_GRID
    case evDestroy: {
      tool_palette_state_t *st = (tool_palette_state_t *)win->userdata;
      if (st && st->strip_tex)
        R_DeleteTexture(st->strip_tex);
      free(st);
      win->userdata = NULL;
      return win_reportview(win, msg, wparam, lparam);
    }
#endif

    case evCommand:
#ifdef FE_TOOLBOX_USE_BUTTON_GRID
      if (HIWORD(wparam) == bxClicked) {
        select_tool_by_ident(win, (int)(int16_t)LOWORD(wparam));
        return true;
      }
#else
      if ((lparam == win) &&
          (HIWORD(wparam) == RVN_SELCHANGE || HIWORD(wparam) == RVN_DBLCLK)) {
        reportview_item_t item = {0};
        if (send_message(win, RVM_GETITEMDATA, LOWORD(wparam), &item))
          select_tool_by_ident(win, (int)item.userdata);
        return true;
      }
#endif
      return false;

    default:
#ifdef FE_TOOLBOX_USE_BUTTON_GRID
      return win_toolbox(win, msg, wparam, lparam);
#else
      return win_reportview(win, msg, wparam, lparam);
#endif
  }
}
