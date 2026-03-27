// Tool palette window
// Uses WINDOW_TOOLBAR for tool buttons (PNG icons from tools.png with wrapping).
// The client area shows FG/BG color swatches and a filled/outline shape mode selector.
// Follows the WinAPI TB_ADDBITMAP / TBBUTTON pattern: one bitmap_strip_t
// shared across all toolbar buttons; each button stores only an icon index (iBitmap).

#include "imageeditor.h"
#include "../../commctl/commctl.h"

// tools.png tile size (all icons are the same size in the strip)
#define ICON_W    16
#define ICON_H    16

// Layout constants for the client-area swatches and fill-mode row.
// Both paint and hit-test must use these same values to stay in sync.
#define PALETTE_LABEL_Y   2   // top of FG/BG labels
#define PALETTE_LABEL_H   8   // height of small text
#define PALETTE_SWATCH_H  16  // height of color swatch boxes
#define PALETTE_FILL_LABEL_H 9 // height of "Fill:" label row
#define PALETTE_FILL_ROW_H   12 // height of Outline/Filled toggle buttons
// Derived: y of the toggle buttons = LABEL_Y + LABEL_H + SWATCH_H + FILL_LABEL_H
#define PALETTE_FILL_ROW_Y \
  (PALETTE_LABEL_Y + PALETTE_LABEL_H + PALETTE_SWATCH_H + PALETTE_FILL_LABEL_H)
// tools.png: 320×16 = 20 columns × 1 row of 16×16 icons.
// Tool order: Pencil(0)..Polygon(9).
// Icon assignments (from visual inspection of tools.png):
//   13=pencil, 15=brush, 12=eraser, 8=fill, 0=select,
//   10=line (diagonal line), 6=rect (cross), 1=ellipse (circle outline),
//   9=rounded-rect (checkerboard-ish), 11=polygon (wavy outline)
static const int k_tool_icon_idx[NUM_TOOLS] = {
  13,   // Pencil
  15,   // Brush
  12,   // Eraser
  8,    // Fill
  0,    // Select
  10,   // Line
  6,    // Rect
  1,    // Ellipse
  9,    // Rounded Rect
  11,   // Polygon
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
      // Client area: FG/BG color swatches + fill mode selector.
      fill_rect(COLOR_PANEL_DARK_BG, 0, 0, win->frame.w, win->frame.h);
      fill_rect(COLOR_DARK_EDGE, win->frame.w - 1, 0, 1, win->frame.h);
      fill_rect(COLOR_DARK_EDGE, 0, win->frame.h - 1, win->frame.w, 1);

      int sy = PALETTE_LABEL_Y;
      draw_text_small("FG", 2, sy, COLOR_TEXT_DISABLED);
      draw_text_small("BG", TB_SPACING+2, sy, COLOR_TEXT_DISABLED);
      sy += PALETTE_LABEL_H;
      if (g_app) {
        #define DrawSwatch(swatch_col, x, color) \
          fill_rect(swatch_col, x+1,  sy - 1, TB_SPACING-2, PALETTE_SWATCH_H); \
          fill_rect(rgba_to_col(color), x+2,  sy, TB_SPACING-4, PALETTE_SWATCH_H-2); 

        DrawSwatch(COLOR_DARK_EDGE, 0, g_app->fg_color);
        DrawSwatch(COLOR_DARK_EDGE, TB_SPACING, g_app->bg_color);

        // Fill mode row: show "Outline" / "Filled" mini toggles
        int fy = sy + PALETTE_SWATCH_H;
        draw_text_small("Fill:", 2, fy, COLOR_TEXT_DISABLED);
        fy += PALETTE_FILL_LABEL_H;
        // Outline button (active when !shape_filled)
        uint32_t outline_col = g_app->shape_filled ? COLOR_BUTTON_BG : COLOR_FOCUSED;
        fill_rect(COLOR_DARK_EDGE,  1,           fy,   TB_SPACING-2, PALETTE_FILL_ROW_H);
        fill_rect(outline_col,      2,           fy+1, TB_SPACING-4, PALETTE_FILL_ROW_H-2);
        draw_text_small("O", 5,                 fy+2, COLOR_TEXT_NORMAL);
        // Filled button (active when shape_filled)
        uint32_t filled_col = g_app->shape_filled ? COLOR_FOCUSED : COLOR_BUTTON_BG;
        fill_rect(COLOR_DARK_EDGE,  TB_SPACING+1, fy, TB_SPACING-2, PALETTE_FILL_ROW_H);
        fill_rect(filled_col,       TB_SPACING+2, fy+1, TB_SPACING-4, PALETTE_FILL_ROW_H-2);
        draw_text_small("F", TB_SPACING+5,       fy+2, COLOR_TEXT_NORMAL);
      }
      return true;
    }

    case kWindowMessageLeftButtonDown: {
      // Check if click is in the fill mode row
      if (!g_app) return false;
      int mx = (int16_t)LOWORD(wparam);
      int my = (int16_t)HIWORD(wparam);
      if (my >= PALETTE_FILL_ROW_Y && my < PALETTE_FILL_ROW_Y + PALETTE_FILL_ROW_H) {
        bool was_filled = g_app->shape_filled;
        g_app->shape_filled = (mx >= TB_SPACING);
        if (g_app->shape_filled != was_filled) {
          invalidate_window(win);
        }
        return true;
      }
      return false;
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
