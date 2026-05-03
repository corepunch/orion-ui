// Project plugins browser for the Orion Form Editor.

#include "formeditor.h"
#include "../../commctl/commctl.h"
#include "../../user/icons.h"

#define PLUGINS_ID_ADD   1
#define PLUGINS_ID_LOAD  2

typedef struct {
  window_t *list_win;
} plugins_browser_state_t;

static const toolbar_item_t kPluginsToolbar[] = {
  { TOOLBAR_ITEM_BUTTON, PLUGINS_ID_ADD,  sysicon_add,  0, 0, "Add plugin" },
  { TOOLBAR_ITEM_BUTTON, PLUGINS_ID_LOAD, sysicon_play, 0, 0, "Load plugin" },
};

static bool plugin_has_dynlib_ext(const char *path) {
  if (!path) return false;
  size_t n = strlen(path);
  size_t e = strlen(AX_DYNLIB_EXT);
  if (n < e) return false;
  return strcmp(path + n - e, AX_DYNLIB_EXT) == 0;
}

static bool plugin_ref_exists(const char *name) {
  if (!g_app || !name || !*name) return false;
  for (int i = 0; i < g_app->project.plugin_count; i++) {
    if (strcmp(g_app->project.plugins[i].name, name) == 0)
      return true;
  }
  return false;
}

static const char *plugin_display_name(const char *name) {
  if (!name || !*name) return "";
  const char *slash = strrchr(name, '/');
  const char *base = slash ? slash + 1 : name;
  size_t base_len = strlen(base);
  static char buf[128];
  const char *exts[] = { AX_DYNLIB_EXT, ".dylib", ".so", ".dll" };
  for (int i = 0; i < (int)ARRAY_LEN(exts); i++) {
    size_t ext_len = strlen(exts[i]);
    if (ext_len > 0 && base_len > ext_len &&
        strcmp(base + base_len - ext_len, exts[i]) == 0) {
      size_t n = MIN(base_len - ext_len, sizeof(buf) - 1);
      memcpy(buf, base, n);
      buf[n] = '\0';
      return buf;
    }
  }
  return base;
}

static bool load_project_plugin(const char *name) {
  if (!name || !*name) return false;
  if (strchr(name, '/') || plugin_has_dynlib_ext(name))
    return fe_load_component_plugin(name);

  char path[4096];
  int n = snprintf(path, sizeof(path), "%s/../lib/%s%s",
                   ui_get_exe_dir(), name, AX_DYNLIB_EXT);
  if (n <= 0 || (size_t)n >= sizeof(path)) return false;
  return fe_load_component_plugin(path);
}

static void plugins_browser_rebuild(plugins_browser_state_t *st) {
  if (!st || !st->list_win) return;

  send_message(st->list_win, RVM_SETREDRAW, 0, NULL);
  send_message(st->list_win, RVM_CLEAR, 0, NULL);

  if (g_app) {
    for (int i = 0; i < g_app->project.plugin_count; i++) {
      reportview_item_t item = {0};
      item.text = plugin_display_name(g_app->project.plugins[i].name);
      item.color = get_sys_color(brTextNormal);
      item.userdata = (uint32_t)i;
      send_message(st->list_win, RVM_ADDITEM, 0, &item);
    }
  }

  send_message(st->list_win, RVM_SETREDRAW, 1, NULL);
}

void plugins_browser_refresh(void) {
  if (!g_app || !g_app->plugins_win) return;
  plugins_browser_state_t *st = (plugins_browser_state_t *)g_app->plugins_win->userdata;
  plugins_browser_rebuild(st);
}

window_t *plugins_browser_create(hinstance_t hinstance) {
  window_t *win = create_window("Plugins",
      WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE | WINDOW_TOOLBAR,
      MAKERECT(PLUGINS_WIN_X, PLUGINS_WIN_Y, PLUGINS_WIN_W, PLUGINS_WIN_H),
      NULL, win_plugins_browser_proc, hinstance, NULL);
  if (win) show_window(win, true);
  return win;
}

static bool plugins_add_ref(const char *name) {
  if (!g_app || !name || !*name) return false;
  if (plugin_ref_exists(name)) return true;
  if (g_app->project.plugin_count >= FE_MAX_PROJECT_PLUGINS) return false;

  form_plugin_ref_t *ref = &g_app->project.plugins[g_app->project.plugin_count++];
  snprintf(ref->name, sizeof(ref->name), "%s", name);
  g_app->project.modified = true;
  plugins_browser_refresh();
  return true;
}

static bool plugins_pick_path(window_t *owner, char *out_path, size_t out_sz) {
  if (!out_path || out_sz == 0) return false;
  openfilename_t ofn = {0};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFile = out_path;
  ofn.nMaxFile = (uint32_t)out_sz;
  ofn.lpstrFilter = "Dynamic Libraries\0*.dylib;*.so;*.dll\0All Files\0*.*\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_FILEMUSTEXIST;
  return get_open_filename(&ofn);
}

static void plugins_add(window_t *owner) {
  char path[512] = {0};
  if (!plugins_pick_path(owner, path, sizeof(path)))
    return;

  if (!load_project_plugin(path)) {
    message_box(owner, "Failed to load plugin.", "Plugins", MB_OK);
    return;
  }
  plugins_add_ref(path);
  formeditor_rebuild_tool_palette();
}

static void plugins_load_selected(plugins_browser_state_t *st, window_t *owner) {
  if (!g_app || !st || !st->list_win) return;
  int sel = (int)send_message(st->list_win, RVM_GETSELECTION, 0, NULL);
  if (sel < 0 || sel >= g_app->project.plugin_count) return;

  const char *name = g_app->project.plugins[sel].name;
  if (!load_project_plugin(name)) {
    message_box(owner, "Failed to load plugin.", "Plugins", MB_OK);
    return;
  }
  formeditor_rebuild_tool_palette();
}

result_t win_plugins_browser_proc(window_t *win, uint32_t msg,
                                  uint32_t wparam, void *lparam) {
  plugins_browser_state_t *st = (plugins_browser_state_t *)win->userdata;
  (void)lparam;
  switch (msg) {
    case evCreate: {
      st = allocate_window_data(win, sizeof(plugins_browser_state_t));
      if (!st)
        return false;

      irect16_t cr = get_client_rect(win);
      st->list_win = create_window(
          "", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL,
          MAKERECT(0, 0, cr.w, cr.h),
          win, win_reportview, 0, NULL);
      if (!st->list_win)
        return false;

      send_message(st->list_win, RVM_SETVIEWMODE, RVM_VIEW_REPORT, NULL);
      send_message(st->list_win, RVM_SETCOLUMNTITLESVISIBLE, 0, NULL);
      {
        reportview_column_t c0 = { "Plugin", 0 };
        send_message(st->list_win, RVM_ADDCOLUMN, 0, &c0);
      }

      send_message(win, tbSetItems, ARRAY_LEN(kPluginsToolbar),
                   (void *)kPluginsToolbar);
      plugins_browser_rebuild(st);
      return true;
    }

    case tbButtonClick:
      switch ((uint16_t)wparam) {
        case PLUGINS_ID_ADD:
          plugins_add(win);
          return true;
        case PLUGINS_ID_LOAD:
          plugins_load_selected(st, win);
          return true;
        default:
          return false;
      }

    case evResize:
      if (st && st->list_win) {
        irect16_t cr = get_client_rect(win);
        resize_window(st->list_win, cr.w, cr.h);
      }
      return false;

    case evCommand:
      return false;

    case evDestroy:
      if (g_app && g_app->plugins_win == win)
        g_app->plugins_win = NULL;
      return false;

    default:
      return false;
  }
}
