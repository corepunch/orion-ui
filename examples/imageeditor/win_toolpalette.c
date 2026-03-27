// Tool palette floating window
// Uses image-based buttons (BUTTON_BITMAP) loaded from tools.png,
// laid out in a 2-column grid matching the Photoshop 1.0 toolbox style.
// This follows the WinAPI BM_SETIMAGE / BUTTON_BITMAP pattern.

#include "imageeditor.h"
#include "../../commctl/commctl.h"

// tools.png dimensions and icon layout
#define TOOLS_TEX_W   32
#define TOOLS_TEX_H  160
#define ICON_W        16
#define ICON_H        16
#define SWATCH_H      26

// Icon positions in tools.png (row, col) for each tool.
// Tool order: Pencil(0), Brush(1), Eraser(2), Fill(3), Select(4).
// tools.png uses the Photoshop 1.0 toolbox order (2 cols x 10 rows of 16x16).
static const int k_tool_icon_row[NUM_TOOLS] = { 5, 5, 6, 3, 0 };
static const int k_tool_icon_col[NUM_TOOLS] = { 0, 1, 1, 0, 0 };

typedef struct {
  GLuint         tools_tex;
  int            sheet_w;   // actual loaded PNG width
  int            sheet_h;   // actual loaded PNG height
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

  if (setjmp(png_jmpbuf(png))) {
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

  png_bytep *rows = malloc(sizeof(png_bytep) * h);
  if (!rows) { png_destroy_read_struct(&png, &info, NULL); fclose(fp); return NULL; }

  png_size_t rowbytes = png_get_rowbytes(png, info);
  for (int r = 0; r < h; r++) {
    rows[r] = malloc(rowbytes);
    if (!rows[r]) {
      for (int i = 0; i < r; i++) free(rows[i]);
      free(rows);
      png_destroy_read_struct(&png, &info, NULL);
      fclose(fp);
      return NULL;
    }
  }
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

  for (int r = 0; r < h; r++) free(rows[r]);
  free(rows);
  png_destroy_read_struct(&png, &info, NULL);
  fclose(fp);

  *out_w = w;
  *out_h = h;
  return rgba;
}

// Try to load tools.png from several candidate locations (share directory).
// Writes the actual texture dimensions to *out_w/*out_h on success.
static GLuint load_tools_texture(int *out_w, int *out_h) {
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
    *out_w = w;
    *out_h = h;
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

      int sheet_w = 0, sheet_h = 0;
      d->tools_tex = load_tools_texture(&sheet_w, &sheet_h);
      d->sheet_w   = sheet_w;
      d->sheet_h   = sheet_h;

      // Create one PUSHLIKE + AUTORADIO button per tool, arranged in 2 columns,
      // matching the Photoshop 1.0 toolbox layout.
      // Buttons use BUTTON_BITMAP when the sprite sheet loaded successfully.
      int col_w = win->frame.w / 2;
      for (int i = 0; i < NUM_TOOLS; i++) {
        int row = i / 2;
        int col = i % 2;
        int bx = col * col_w + 1;
        int by = row * TOOL_ICON_ROW_H;
        int bw = col_w - 2;
        int bh = TOOL_ICON_ROW_H - 2;

        uint32_t flags = WINDOW_NOTITLE | WINDOW_NOFILL |
                         BUTTON_PUSHLIKE | BUTTON_AUTORADIO;
        if (d->tools_tex) flags |= BUTTON_BITMAP;

        window_t *btn = create_window(
            tool_names[i], flags,
            MAKERECT(bx, by, bw, bh),
            win, win_button, NULL);
        btn->id    = (uint16_t)(ID_TOOL_PENCIL + i);
        btn->value = (btn->id == ID_TOOL_PENCIL);

        if (d->tools_tex) {
          // Use the actual loaded sheet dimensions so UV math stays correct even
          // if a differently-sized tools.png is found on one of the fallback paths.
          button_image_t img = {
            .tex      = (uint32_t)d->tools_tex,
            .icon_col = k_tool_icon_col[i],
            .icon_row = k_tool_icon_row[i],
            .icon_w   = ICON_W,
            .icon_h   = ICON_H,
            .sheet_w  = d->sheet_w,
            .sheet_h  = d->sheet_h,
          };
          // kButtonMessageSetImage makes a private copy; the local img can be
          // stack-allocated here.
          send_message(btn, kButtonMessageSetImage, 0, &img);
        }

        show_window(btn, true);
      }
      return true;
    }

    case kWindowMessageDestroy: {
      // Children are destroyed AFTER this handler returns (clear_window_children
      // is called by destroy_window after sending kWindowMessageDestroy to the
      // parent). Because each child button owns a private copy of button_image_t
      // (via kButtonMessageSetImage), it is safe to free the palette's data and
      // delete the GL texture here without leaving buttons with dangling pointers.
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
      fill_rect(COLOR_PANEL_DARK_BG, 0, 0, win->frame.w, win->frame.h);
      fill_rect(COLOR_DARK_EDGE, win->frame.w - 1, 0, 1, win->frame.h);
      fill_rect(COLOR_DARK_EDGE, 0, win->frame.h - 1, win->frame.w, 1);

      // Color swatches below the tool buttons
      int num_tool_rows = (NUM_TOOLS + 1) / 2;
      int sy = num_tool_rows * TOOL_ICON_ROW_H + 2;
      draw_text_small("FG", 2, sy, COLOR_TEXT_DISABLED);
      draw_text_small("BG", 34, sy, COLOR_TEXT_DISABLED);
      sy += 8;
      if (g_app) {
        fill_rect(COLOR_DARK_EDGE, 1,  sy - 1, 28, 14);
        fill_rect(rgba_to_col(g_app->fg_color), 2,  sy, 26, 12);
        fill_rect(COLOR_DARK_EDGE, 33, sy - 1, 28, 14);
        fill_rect(rgba_to_col(g_app->bg_color), 34, sy, 26, 12);
      }
      return false; // allow children (buttons) to paint themselves
    }

    case kWindowMessageCommand: {
      // Button children send kWindowMessageCommand with kButtonNotificationClicked
      // to their root window (this window).  Forward the command to the menubar
      // so that both button clicks and accelerator hotkeys are handled in one place.
      if (HIWORD(wparam) == kButtonNotificationClicked && g_app) {
        send_message(g_app->menubar_win, kWindowMessageCommand,
                     MAKEDWORD(LOWORD(wparam), kButtonNotificationClicked), lparam);
      }
      return true;
    }

    default:
      return false;
  }
}
