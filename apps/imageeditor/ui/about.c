// About dialog - auto-layout form with a banner image on the left and
// application info on the right.

#include "imageeditor.h"

// State

typedef struct {
  GLuint banner_tex;
} about_state_t;

static GLuint load_banner_texture(void) {
  const char *found = NULL;
#ifdef SHAREDIR
  char bundled[4096];
  int n = snprintf(bundled, sizeof(bundled), "%s/" SHAREDIR "/conan.png",
                   ui_get_exe_dir());
  if (n >= 0 && (size_t)n < sizeof(bundled)) {
    FILE *f = fopen(bundled, "rb");
    if (f) { fclose(f); found = bundled; }
  }
#endif
  if (!found) {
    static const char *kSourceTreeBanner = "apps/imageeditor/share/conan.png";
    FILE *f = fopen(kSourceTreeBanner, "rb");
    if (f) { fclose(f); found = kSourceTreeBanner; }
  }
  if (!found) return 0;

  int w = 0, h = 0;
  uint8_t *pixels = load_image(found, &w, &h);
  if (!pixels) return 0;

  GLuint tex = R_CreateTextureSRGBA8(w, h, pixels, R_FILTER_LINEAR, R_WRAP_CLAMP);
  image_free(pixels);

  return tex;
}

// Dialog window procedure

static result_t about_proc(window_t *win, uint32_t msg,
                            uint32_t wparam, void *lparam) {
  about_state_t *st = (about_state_t *)win->userdata;

  switch (msg) {
    case evCreate: {
      about_state_t *s = allocate_window_data(win, sizeof(about_state_t));
      s->banner_tex = load_banner_texture();
      window_t *banner = get_window_item(win, ID_ABOUT_BANNER);
      if (banner)
        banner->userdata = (void *)(uintptr_t)s->banner_tex;
      return true;
    }

    case evCommand: {
      if (HIWORD(wparam) == btnClicked) {
        end_dialog(win, 1);
        return true;
      }
      return false;
    }

    case evDestroy: {
      if (st && st->banner_tex) {
        R_DeleteTexture(st->banner_tex);
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
  show_dialog_from_form(&imageeditor_about_form, "About Orion Image Editor",
                        parent, about_proc, NULL);
}
