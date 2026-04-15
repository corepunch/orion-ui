// File picker dialog (modal, PNG-filtered) — thin wrapper around the
// framework's generic get_open_filename() / get_save_filename().

#include "imageeditor.h"

// Wrapper kept for backward compatibility with win_menubar.c.
static bool show_file_picker(window_t *parent, bool save_mode,
                              char *out_path, size_t out_sz) {
  openfilename_t ofn = {0};
  ofn.lStructSize  = sizeof(ofn);
  ofn.hwndOwner    = parent;
  ofn.lpstrFile    = out_path;
  ofn.nMaxFile     = (uint32_t)out_sz;
  ofn.lpstrFilter  = "Image Files\0*.png;*.jpg;*.jpeg;*.bmp\0PNG Files\0*.png\0JPEG Files\0*.jpg;*.jpeg\0All Files\0*.*\0";
  ofn.nFilterIndex = 1;
  ofn.Flags        = save_mode ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST;

  if (save_mode)
    return get_save_filename(&ofn);
  else
    return get_open_filename(&ofn);
}


