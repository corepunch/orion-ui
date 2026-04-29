#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "browser.h"

#define BROWSER_SETTINGS_FILE "browser.ini"
#define BROWSER_DEFAULT_HOME  "https://example.com"

#define ID_DLG_HOME_EDIT   3101
#define ID_DLG_SAVE        3102
#define ID_DLG_CANCEL      3103

#define ID_ABOUT_OK        3201

typedef struct {
  browser_state_t *st;
} browser_settings_dialog_state_t;

static const form_ctrl_def_t kSettingsChildren[] = {
  { FORM_CTRL_LABEL,    -1,              {8,   9, 80, 13},   0,              "Home URL:", "lbl_home"   },
  { FORM_CTRL_TEXTEDIT, ID_DLG_HOME_EDIT,{68,  7, 268, 16},  0,              "",          "edit_home"  },
  { FORM_CTRL_BUTTON,   ID_DLG_SAVE,     {218, 34, 50, 18},  BUTTON_DEFAULT, "Save",      "btn_save"   },
  { FORM_CTRL_BUTTON,   ID_DLG_CANCEL,   {276, 34, 60, 18},  0,              "Cancel",    "btn_cancel" },
};

static const form_def_t kSettingsForm = {
  .name = "Browser Settings",
  .width = 344,
  .height = 62,
  .children = kSettingsChildren,
  .child_count = (int)(sizeof(kSettingsChildren) / sizeof(kSettingsChildren[0])),
};

static const form_ctrl_def_t kAboutChildren[] = {
  { FORM_CTRL_LABEL,  -1,          {8,  8, 210, 13},  0,              "Orion Browser",            "lbl_title" },
  { FORM_CTRL_LABEL,  -1,          {8, 24, 210, 13},  0,              "Version 0.2",              "lbl_version" },
  { FORM_CTRL_LABEL,  -1,          {8, 40, 220, 26},  0,              "Minimal HTML browser with local file support.", "lbl_desc" },
  { FORM_CTRL_BUTTON, ID_ABOUT_OK, {84, 72, 60, 18},  BUTTON_DEFAULT, "OK",                       "btn_ok" },
};

static const form_def_t kAboutForm = {
  .name = "About Browser",
  .width = 236,
  .height = 98,
  .children = kAboutChildren,
  .child_count = (int)(sizeof(kAboutChildren) / sizeof(kAboutChildren[0])),
  .ok_id = ID_ABOUT_OK,
};

static rect_t browser_centered_settings_rect(window_t *parent) {
  flags_t flags = WINDOW_DIALOG | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE;
  rect_t wr = {0, 0, kSettingsForm.width, kSettingsForm.height};

  adjust_window_rect(&wr, flags);
  return center_window_rect(wr, parent);
}

static void trim_copy_url(char *dst, size_t dst_sz, const char *src) {
  if (!dst || dst_sz == 0) return;
  dst[0] = '\0';
  if (!src) return;

  while (*src && isspace((unsigned char)*src)) src++;
  size_t n = strlen(src);
  while (n > 0 && isspace((unsigned char)src[n - 1])) n--;
  if (n == 0) return;

  snprintf(dst, dst_sz, "%.*s", (int)n, src);
}

void browser_settings_init(browser_state_t *st) {
  if (!st) return;
  snprintf(st->home_url, sizeof(st->home_url), "%s", BROWSER_DEFAULT_HOME);
}

bool browser_settings_load(browser_state_t *st) {
  if (!st) return false;

  char buf[2048];
  size_t n = 0;
  if (!axSettingsLoad(BROWSER_SETTINGS_FILE, buf, sizeof(buf) - 1, &n)) return false;
  buf[n] = '\0';

  bool in_browser_section = false;
  char *line = buf;
  while (*line) {
    char *next = strchr(line, '\n');
    if (next) {
      *next = '\0';
      next++;
    }

    while (*line && isspace((unsigned char)*line)) line++;
    char *end = line + strlen(line);
    while (end > line && isspace((unsigned char)end[-1])) *--end = '\0';

    if (*line == '\0' || *line == ';' || *line == '#') {
      line = next ? next : end;
      continue;
    }

    if (*line == '[') {
      in_browser_section = (strcmp(line, "[browser]") == 0);
      line = next ? next : end;
      continue;
    }

    if (in_browser_section) {
      char *eq = strchr(line, '=');
      if (eq) {
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        while (*key && isspace((unsigned char)*key)) key++;
        char *kend = key + strlen(key);
        while (kend > key && isspace((unsigned char)kend[-1])) *--kend = '\0';

        while (*val && isspace((unsigned char)*val)) val++;

        if (strcmp(key, "home_url") == 0) {
          char home[sizeof(st->home_url)];
          trim_copy_url(home, sizeof(home), val);
          if (home[0])
            snprintf(st->home_url, sizeof(st->home_url), "%s", home);
        }
      }
    }

    line = next ? next : end;
  }

  return true;
}

bool browser_settings_save(const browser_state_t *st) {
  if (!st) return false;

  char text[1400];
  int n = snprintf(
    text,
    sizeof(text),
    "; Orion browser settings\n"
    "[browser]\n"
    "home_url=%s\n",
    st->home_url[0] ? st->home_url : BROWSER_DEFAULT_HOME
  );
  if (n <= 0 || (size_t)n >= sizeof(text)) return false;

  return axSettingsSave(BROWSER_SETTINGS_FILE, text, (size_t)n) ? true : false;
}

static result_t browser_settings_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  browser_settings_dialog_state_t *ds = (browser_settings_dialog_state_t *)win->userdata;

  switch (msg) {
    case evCreate:
      win->userdata = lparam;
      ds = (browser_settings_dialog_state_t *)win->userdata;
      if (ds && ds->st) {
        ds->st->settings_win = win;
        set_window_item_text(win, ID_DLG_HOME_EDIT, "%s", ds->st->home_url);
      }
      return true;

    case evClose:
      destroy_window(win);
      return true;

    case evCommand:
      if (HIWORD(wparam) != btnClicked) return false;
      if (LOWORD(wparam) == ID_DLG_CANCEL) {
        destroy_window(win);
        return true;
      }
      if (LOWORD(wparam) == ID_DLG_SAVE) {
        window_t *home = get_window_item(win, ID_DLG_HOME_EDIT);
        if (home && ds && ds->st) {
          char trimmed[sizeof(ds->st->home_url)];
          trim_copy_url(trimmed, sizeof(trimmed), home->title);
          if (trimmed[0])
            snprintf(ds->st->home_url, sizeof(ds->st->home_url), "%s", trimmed);
          browser_settings_save(ds->st);
        }
        destroy_window(win);
        return true;
      }
      return false;

    case evDestroy:
      if (ds && ds->st && ds->st->settings_win == win)
        ds->st->settings_win = NULL;
      free(ds);
      win->userdata = NULL;
      return true;

    default:
      return false;
  }
}

bool browser_show_settings_window(window_t *parent, browser_state_t *st) {
  if (!st) return false;
  if (st->settings_win && is_window(st->settings_win)) {
    move_to_top(st->settings_win);
    set_focus(st->settings_win);
    return true;
  }

  browser_settings_dialog_state_t *ds = malloc(sizeof(*ds));
  if (!ds) return false;
  ds->st = st;

  form_def_t settings_def = kSettingsForm;
  rect_t wr = browser_centered_settings_rect(parent);

  settings_def.flags |= WINDOW_DIALOG | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE;
  settings_def.width = wr.w;
  settings_def.height = wr.h;

  window_t *win = create_window_from_form(&settings_def, wr.x, wr.y,
                                          NULL, browser_settings_proc,
                                          parent ? get_root_window(parent)->hinstance : 0,
                                          ds);
  if (!win) {
    free(ds);
    return false;
  }

  show_window(win, true);
  move_to_top(win);
  set_focus(win);
  return true;
}

bool browser_pick_open_path(window_t *parent, char *out_path, size_t out_sz) {
  openfilename_t ofn = {0};

  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = parent;
  ofn.lpstrFile = out_path;
  ofn.nMaxFile = (uint32_t)out_sz;
  ofn.lpstrFilter = "HTML Files\0*.html;*.htm\0All Files\0*.*\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_FILEMUSTEXIST;

  return get_open_filename(&ofn);
}

bool browser_pick_save_path(window_t *parent, char *out_path, size_t out_sz) {
  openfilename_t ofn = {0};

  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = parent;
  ofn.lpstrFile = out_path;
  ofn.nMaxFile = (uint32_t)out_sz;
  ofn.lpstrFilter = "HTML Files\0*.html;*.htm\0All Files\0*.*\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_OVERWRITEPROMPT;

  return get_save_filename(&ofn);
}

void browser_show_about_dialog(window_t *parent) {
  show_ddx_dialog(&kAboutForm, "About Browser", parent, NULL);
}
