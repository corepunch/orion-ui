// About dialog – displays a banner image (conan.png) on the left
// and application info on the right, with an OK button to close.

#include "imageeditor.h"

// ──────────────────────────────────────────────────────────────────
// Dialog geometry
// ──────────────────────────────────────────────────────────────────

#define ABOUT_WIN_W    270
#define ABOUT_WIN_H    120

// Banner panel on the left (maintains conan.png aspect ratio ~1024:1063)
#define ABOUT_BANNER_W  ABOUT_WIN_H
#define ABOUT_BANNER_H  ABOUT_WIN_H

// OK button
#define ABOUT_BTN_W     50
#define ABOUT_BTN_H     13

// Text column starts to the right of the banner + separator
#define ABOUT_TEXT_X   (ABOUT_BANNER_W + 8)

// Width available for labels in the right column
#define ABOUT_LABEL_W  (ABOUT_WIN_W - ABOUT_TEXT_X - 4)

// ──────────────────────────────────────────────────────────────────
// State
// ──────────────────────────────────────────────────────────────────

typedef struct {
  GLuint banner_tex;
} about_state_t;

// ──────────────────────────────────────────────────────────────────
// Banner loader – searches several relative paths for conan.png
// ──────────────────────────────────────────────────────────────────

static const char *kBannerPaths[] = {
  "examples/imageeditor/conan.png",       // run from repo root
  "../../examples/imageeditor/conan.png", // run from build/bin/
  "../examples/imageeditor/conan.png",    // run from build/
  "conan.png",                            // same directory
};

static GLuint load_banner_texture(void) {
  const char *found = NULL;
  for (int i = 0; i < (int)(sizeof(kBannerPaths)/sizeof(kBannerPaths[0])); i++) {
    FILE *f = fopen(kBannerPaths[i], "rb");
    if (f) { fclose(f); found = kBannerPaths[i]; break; }
  }
  if (!found) return 0;

  FILE *fp = fopen(found, "rb");
  if (!fp) return 0;

  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png) { fclose(fp); return 0; }

  png_infop info = png_create_info_struct(png);
  if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(fp); return 0; }

  // Declared volatile so their values survive longjmp in the error path.
  volatile uint8_t   *pixels = NULL;
  volatile png_bytep *rows   = NULL;

  if (setjmp(png_jmpbuf(png))) {
    // longjmp error path: free any heap buffers already allocated.
    free((void *)rows);
    free((void *)pixels);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return 0;
  }

  png_init_io(png, fp);
  png_read_info(png, info);

  int w = (int)png_get_image_width(png, info);
  int h = (int)png_get_image_height(png, info);
  png_byte ct = png_get_color_type(png, info);
  png_byte bd = png_get_bit_depth(png, info);

  if (bd == 16) png_set_strip_16(png);
  if (ct == PNG_COLOR_TYPE_PALETTE)                       png_set_palette_to_rgb(png);
  if (ct == PNG_COLOR_TYPE_GRAY && bd < 8)                png_set_expand_gray_1_2_4_to_8(png);
  if (png_get_valid(png, info, PNG_INFO_tRNS))            png_set_tRNS_to_alpha(png);
  if (ct == PNG_COLOR_TYPE_RGB  ||
      ct == PNG_COLOR_TYPE_GRAY ||
      ct == PNG_COLOR_TYPE_PALETTE)                       png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
  if (ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_GRAY_ALPHA)
                                                          png_set_gray_to_rgb(png);
  png_read_update_info(png, info);

  size_t rowbytes = png_get_rowbytes(png, info);
  pixels = malloc((size_t)h * rowbytes);
  rows   = malloc(sizeof(png_bytep) * (size_t)h);
  if (!pixels || !rows) {
    free((void *)rows);
    free((void *)pixels);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return 0;
  }

  for (int r = 0; r < h; r++)
    rows[r] = (uint8_t *)pixels + (size_t)r * rowbytes;

  png_read_image(png, (png_bytepp)rows);
  free((void *)rows);
  rows = NULL;
  png_destroy_read_struct(&png, &info, NULL);
  fclose(fp);

  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, (void *)pixels);
  free((void *)pixels);

  return tex;
}

// ──────────────────────────────────────────────────────────────────
// Helpers to create labeled sub-windows
// ──────────────────────────────────────────────────────────────────

#define DIM  ((void *)(uintptr_t)COLOR_TEXT_DISABLED)

static void make_label(window_t *parent, const char *text, int y, void *color) {
  create_window(text, WINDOW_NOTITLE | WINDOW_NOFILL,
                MAKERECT(ABOUT_TEXT_X, y, ABOUT_LABEL_W, CONTROL_HEIGHT),
                parent, win_label, color);
}

// ──────────────────────────────────────────────────────────────────
// Dialog window procedure
// ──────────────────────────────────────────────────────────────────

static result_t about_proc(window_t *win, uint32_t msg,
                            uint32_t wparam, void *lparam) {
  about_state_t *st = (about_state_t *)win->userdata;

  switch (msg) {
    case kWindowMessageCreate: {
      about_state_t *s = allocate_window_data(win, sizeof(about_state_t));
      s->banner_tex = load_banner_texture();

      // Banner image (left panel)
      create_window("", WINDOW_NOTITLE | WINDOW_NOFILL,
                    MAKERECT(0, 0, ABOUT_BANNER_W, ABOUT_BANNER_H),
                    win, win_image, (void *)(uintptr_t)s->banner_tex);

      // App info labels (right column)
      make_label(win, "Orion Image Editor",  8, NULL);
      make_label(win, "Version 1.0",        22, DIM);
      make_label(win, "A MacPaint-inspired", 40, DIM);
      make_label(win, "pixel art editor.",   50, DIM);
      make_label(win, "Built with the",      66, DIM);
      make_label(win, "Orion UI framework.", 76, DIM);

      // OK button (centered at the bottom)
      int bx = ABOUT_WIN_W - ABOUT_BTN_W - 4;
      int by = ABOUT_WIN_H - ABOUT_BTN_H - 4;
      create_window("OK", 0,
                    MAKERECT(bx, by, ABOUT_BTN_W, ABOUT_BTN_H),
                    win, win_button, NULL);
      return true;
    }

    case kWindowMessageCommand: {
      if (HIWORD(wparam) == kButtonNotificationClicked) {
        end_dialog(win, 1);
        return true;
      }
      return false;
    }

    case kWindowMessageDestroy: {
      if (st && st->banner_tex) {
        glDeleteTextures(1, &st->banner_tex);
        st->banner_tex = 0;
      }
      return false;
    }

    default:
      return false;
  }
}

// ──────────────────────────────────────────────────────────────────
// Public entry point
// ──────────────────────────────────────────────────────────────────

void show_about_dialog(window_t *parent) {
  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
  int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
  int x  = (sw - ABOUT_WIN_W) / 2;
  int y  = (sh - ABOUT_WIN_H) / 2;
  show_dialog("About Orion Image Editor",
              MAKERECT(x, y, ABOUT_WIN_W, ABOUT_WIN_H),
              parent, about_proc, NULL);
}
