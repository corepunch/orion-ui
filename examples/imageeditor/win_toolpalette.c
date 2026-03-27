// Tool palette window
// Uses WINDOW_TOOLBAR for tool buttons (PNG icons from tools.png with wrapping).
// The client area shows FG/BG color swatches.
// Follows the WinAPI TB_ADDBITMAP / TBBUTTON pattern: one bitmap_strip_t
// shared across all toolbar buttons; each button stores only an icon index (iBitmap).

#include "imageeditor.h"
#include "../../commctl/commctl.h"

// tools.png tile size (all icons are the same size in the strip)
#define ICON_W    16
#define ICON_H    16

// Icon index = row * cols + col, where cols = sheet_w / ICON_W.
// tools.png: 32×160 = 2 columns × 10 rows of 16×16 icons.
// Tool order: Pencil(0), Brush(1), Eraser(2), Fill(3), Select(4).
static const int k_tool_icon_idx[NUM_TOOLS] = {
  5*2+0,   // Pencil:  row 5, col 0  → index 10
  5*2+1,   // Brush:   row 5, col 1  → index 11
  6*2+1,   // Eraser:  row 6, col 1  → index 13
  3*2+0,   // Fill:    row 3, col 0  → index  6
  0*2+0,   // Select:  row 0, col 0  → index  0
};

typedef struct {
  GLuint         tools_tex;
  bitmap_strip_t strip;     // shared strip descriptor, owned by palette
} tool_palette_data_t;

// Load a PNG file and return heap-allocated RGBA pixels (caller frees),
// or NULL on failure.  Writes image dimensions to *out_w / *out_h.
static uint8_t *load_png_rgba(const char *path, int *out_w, int *out_h) {
  FILE *fp = fopen(path, "rb");
  if (!fp) return NULL;

  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png) { fclose(fp); return NULL; }
  png_infop info = png_create_info_struct(png);
  if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(fp); return NULL; }

  // Declare volatile so that the setjmp error path can safely free them after
  // a libpng longjmp, even if allocation happened after the setjmp call.
  uint8_t * volatile pixel_buf = NULL;
  png_bytep * volatile rows = NULL;

  if (setjmp(png_jmpbuf(png))) {
    free(pixel_buf);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return NULL;
  }

  png_init_io(png, fp);
  png_read_info(png, info);

  int w = (int)png_get_image_width(png, info);
  int h = (int)png_get_image_height(png, info);
  png_byte ct = png_get_color_type(png, info);
  png_byte bd = png_get_bit_depth(png, info);

  if (bd == 16) png_set_strip_16(png);
  if (ct == PNG_COLOR_TYPE_PALETTE)      png_set_palette_to_rgb(png);
  if (ct == PNG_COLOR_TYPE_GRAY && bd<8) png_set_expand_gray_1_2_4_to_8(png);
  if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
  if (ct == PNG_COLOR_TYPE_RGB ||
      ct == PNG_COLOR_TYPE_GRAY ||
      ct == PNG_COLOR_TYPE_PALETTE) png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
  if (ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb(png);
  png_read_update_info(png, info);

  // Use a single contiguous pixel buffer so the setjmp error handler only
  // needs to free two allocations regardless of how many rows exist.
  png_size_t rowbytes = png_get_rowbytes(png, info);
  pixel_buf = malloc((size_t)rowbytes * h);
  rows      = malloc(sizeof(png_bytep) * h);
  if (!pixel_buf || !rows) {
    free(pixel_buf);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return NULL;
  }
  for (int r = 0; r < h; r++)
    rows[r] = pixel_buf + (size_t)r * rowbytes;

  png_read_image(png, rows);

  // Convert to RGBA: treat the source as black-on-white artwork.
  // Use inverted luminance as alpha so that black icon lines are fully
  // opaque and the white background is transparent.  Icon pixels are
  // rendered in COLOR_TEXT_NORMAL (light gray) so they are visible on
  // the dark panel background.
  uint8_t *rgba = malloc((size_t)w * h * 4);
  if (rgba) {
    for (int row = 0; row < h; row++) {
      for (int col = 0; col < w; col++) {
        png_bytep px = &rows[row][col * 4];
        uint8_t src_r = px[0], src_g = px[1], src_b = px[2];
        uint8_t lum = (uint8_t)(((int)src_r * 77 + (int)src_g * 150 + (int)src_b * 29) >> 8);
        uint8_t alpha = (uint8_t)(255 - lum);
        rgba[(row * w + col) * 4 + 0] = 0xC0;
        rgba[(row * w + col) * 4 + 1] = 0xC0;
        rgba[(row * w + col) * 4 + 2] = 0xC0;
        rgba[(row * w + col) * 4 + 3] = alpha;
      }
    }
  }

  free(rows);
  free(pixel_buf);
  png_destroy_read_struct(&png, &info, NULL);
  fclose(fp);

  *out_w = w;
  *out_h = h;
  return rgba;
}

// Try to load tools.png from several candidate locations (share directory).
// On success, populates *strip with the loaded texture and tile geometry.
// Returns the GL texture ID (also stored in strip->tex), or 0 on failure.
static GLuint load_tools_texture(bitmap_strip_t *strip) {
  static const char *k_paths[] = {
    "build/share/tools.png",             // run from repository root
    "../share/tools.png",                // run from build/bin/
    "share/tools.png",                   // run from build/
    "examples/imageeditor/tools.png",    // fallback: source tree
    NULL
  };

  for (int i = 0; k_paths[i]; i++) {
    int w = 0, h = 0;
    uint8_t *rgba = load_png_rgba(k_paths[i], &w, &h);
    if (!rgba) continue;

    // Validate that the PNG tiles evenly into ICON_W × ICON_H cells.
    // If cols would be 0 (w < ICON_W) or the dimensions don't divide evenly,
    // this file is unusable — skip it and try the next fallback path.
    if (w < ICON_W || h < ICON_H || (w % ICON_W) != 0 || (h % ICON_H) != 0) {
      free(rgba);
      continue;
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);

    strip->tex     = (uint32_t)tex;
    strip->icon_w  = ICON_W;
    strip->icon_h  = ICON_H;
    strip->cols    = w / ICON_W;  // number of icon columns in the strip
    strip->sheet_w = w;
    strip->sheet_h = h;
    return tex;
  }
  return 0;
}

result_t win_tool_palette_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessageCreate: {
      tool_palette_data_t *d =
          (tool_palette_data_t *)allocate_window_data(win, sizeof(tool_palette_data_t));

      d->tools_tex = load_tools_texture(&d->strip);

      // Associate the PNG strip with the window's toolbar.
      // The toolbar rendering in message.c will use this strip for icon display.
      if (d->tools_tex) {
        send_message(win, kToolBarMessageSetStrip, 0, &d->strip);
      }

      // Add one toolbar button per tool.
      // icon = PNG strip index (iBitmap); ident = tool command ID.
      toolbar_button_t buttons[NUM_TOOLS];
      for (int i = 0; i < NUM_TOOLS; i++) {
        buttons[i].icon   = k_tool_icon_idx[i];
        buttons[i].ident  = ID_TOOL_PENCIL + i;
        buttons[i].active = (i == 0);  // Pencil is selected by default
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

    case kWindowMessagePaint: {
      // Client area: FG/BG color swatches.
      fill_rect(COLOR_PANEL_DARK_BG, 0, 0, win->frame.w, win->frame.h);
      fill_rect(COLOR_DARK_EDGE, win->frame.w - 1, 0, 1, win->frame.h);
      fill_rect(COLOR_DARK_EDGE, 0, win->frame.h - 1, win->frame.w, 1);

      int sy = 2;
      draw_text_small("FG", 2, sy, COLOR_TEXT_DISABLED);
      draw_text_small("BG", 34, sy, COLOR_TEXT_DISABLED);
      sy += 8;
      if (g_app) {
        fill_rect(COLOR_DARK_EDGE, 1,  sy - 1, 28, 14);
        fill_rect(rgba_to_col(g_app->fg_color), 2,  sy, 26, 12);
        fill_rect(COLOR_DARK_EDGE, 33, sy - 1, 28, 14);
        fill_rect(rgba_to_col(g_app->bg_color), 34, sy, 26, 12);
      }
      return true;
    }

    case kToolBarMessageButtonClick: {
      // A toolbar button was clicked: update the active button state and
      // forward the command to the menubar (same path as keyboard accelerators).
      int clicked_ident = (int)wparam;
      send_message(win, kToolBarMessageSetActiveButton, (uint32_t)clicked_ident, NULL);
      if (g_app) {
        send_message(g_app->menubar_win, kWindowMessageCommand,
                     MAKEDWORD((uint16_t)clicked_ident, kButtonNotificationClicked), lparam);
      }
      return true;
    }

    default:
      return false;
  }
}
