// Tool palette — loads toolbox.png (23x23 icons, VB3 toolbox order)
// and presents them via WINDOW_TOOLBAR with a custom TOOLBOX_BTN_SIZE.

#include "formeditor.h"
#include "../../commctl/commctl.h"

// VB3 toolbox strip indices for each tool in display order.
// Strip layout (3 cols x 13 rows, 23x23): index = row*3+col.
// VB3 canonical order: 0=Pointer, 1=PictureBox, 2=Label, 3=TextBox,
//   4=Frame, 5=CommandButton, 6=CheckBox, 7=OptionButton,
//   8=ComboBox, 9=ListBox, ...
static const int k_tool_order[NUM_TOOLS] = {
  ID_TOOL_SELECT,
  ID_TOOL_LABEL,
  ID_TOOL_TEXTEDIT,
  ID_TOOL_BUTTON,
  ID_TOOL_CHECKBOX,
  ID_TOOL_COMBOBOX,
  ID_TOOL_LIST,
};

static const int k_tool_icon[NUM_TOOLS] = {
  0,   // Pointer/Select
  2,   // Label
  3,   // TextBox
  5,   // CommandButton
  6,   // CheckBox
  8,   // ComboBox
  9,   // ListBox
};

typedef struct {
  GLuint         tools_tex;
  bitmap_strip_t strip;
} tool_palette_data_t;

static uint8_t *load_png_rgba(const char *path, int *out_w, int *out_h) {
  int w = 0, h = 0;
  uint8_t *src = load_image(path, &w, &h);
  if (!src) return NULL;
  uint8_t *rgba = malloc((size_t)w * h * 4);
  if (rgba) {
    for (int i = 0; i < w * h; i++) {
      uint8_t r = src[i*4+0], g = src[i*4+1], b = src[i*4+2];
      uint8_t lum = (uint8_t)(((int)r*77 + (int)g*150 + (int)b*29) >> 8);
      uint8_t alpha = (uint8_t)(255 - lum);
      rgba[i*4+0] = 0xC0;
      rgba[i*4+1] = 0xC0;
      rgba[i*4+2] = 0xC0;
      rgba[i*4+3] = alpha;
    }
  }
  image_free(src);
  *out_w = w; *out_h = h;
  return rgba;
}

static GLuint load_tools_texture(bitmap_strip_t *strip) {
#ifdef SHAREDIR
  char path[4096];
  snprintf(path, sizeof(path), "%s/" SHAREDIR "/toolbox.png", ui_get_exe_dir());
  int w = 0, h = 0;
  uint8_t *rgba = load_png_rgba(path, &w, &h);
  if (!rgba) return 0;
  if (w < TOOLBOX_ICON_W || h < TOOLBOX_ICON_H ||
      (w % TOOLBOX_ICON_W) != 0 || (h % TOOLBOX_ICON_H) != 0) {
    free(rgba); return 0;
  }
  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  free(rgba);
  strip->tex     = (uint32_t)tex;
  strip->icon_w  = TOOLBOX_ICON_W;
  strip->icon_h  = TOOLBOX_ICON_H;
  strip->cols    = w / TOOLBOX_ICON_W;
  strip->sheet_w = w;
  strip->sheet_h = h;
  return tex;
#else
  (void)strip;
  return 0;
#endif
}

result_t win_tool_palette_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessageCreate: {
      tool_palette_data_t *d =
          (tool_palette_data_t *)allocate_window_data(win, sizeof(tool_palette_data_t));
      // Set button size before adding buttons so titlebar_height() is correct.
      send_message(win, kToolBarMessageSetButtonSize, TOOLBOX_BTN_SIZE, NULL);
      d->tools_tex = load_tools_texture(&d->strip);
      if (d->tools_tex)
        send_message(win, kToolBarMessageSetStrip, 0, &d->strip);
      toolbar_button_t buttons[NUM_TOOLS];
      for (int i = 0; i < NUM_TOOLS; i++) {
        buttons[i].icon   = k_tool_icon[i];
        buttons[i].ident  = k_tool_order[i];
        buttons[i].active = (k_tool_order[i] == ID_TOOL_SELECT);
      }
      send_message(win, kToolBarMessageAddButtons, NUM_TOOLS, buttons);
      return true;
    }
    case kWindowMessageDestroy: {
      tool_palette_data_t *d = (tool_palette_data_t *)win->userdata;
      if (d && d->tools_tex) {
        glDeleteTextures(1, &d->tools_tex);
        d->tools_tex = 0;
      }
      free(win->userdata);
      win->userdata = NULL;
      return true;
    }
    case kWindowMessagePaint:
      fill_rect(get_sys_color(kColorWindowDarkBg), win->frame.x, win->frame.y,
                win->frame.w, win->frame.h);
      return true;
    case kToolBarMessageButtonClick: {
      int ident = (int)wparam;
      send_message(win, kToolBarMessageSetActiveButton, (uint32_t)ident, NULL);
      if (g_app) {
        g_app->current_tool = ident;
        if (g_app->doc && g_app->doc->canvas_win)
          invalidate_window(g_app->doc->canvas_win);
        if (g_app->menubar_win)
          send_message(g_app->menubar_win, kWindowMessageCommand,
                       MAKEDWORD((uint16_t)ident, kButtonNotificationClicked), lparam);
        else
          handle_menu_command((uint16_t)ident);
      }
      return true;
    }
    default:
      return false;
  }
}
