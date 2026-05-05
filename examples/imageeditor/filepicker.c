// File picker dialog — thin wrapper around the framework's
// get_open_filename() / get_save_filename().
// In indexed (256-colour) mode only PCX and BMP are offered.
// In 32-bit mode PNG, JPEG, and BMP are offered.

#include "imageeditor.h"

// Wrapper kept for backward compatibility with win_menubar.c.
bool show_file_picker(window_t *parent, bool save_mode,
                      char *out_path, size_t out_sz) {
  openfilename_t ofn = {0};
  ofn.lStructSize  = sizeof(ofn);
  ofn.hwndOwner    = parent;
  ofn.lpstrFile    = out_path;
  ofn.nMaxFile     = (uint32_t)out_sz;
#if IMAGEEDITOR_INDEXED
  ofn.lpstrFilter  = "Image Files\0*.pcx;*.bmp\0"
                     "PCX Files\0*.pcx\0"
                     "BMP Files\0*.bmp\0"
                     "All Files\0*.*\0";
#else
  ofn.lpstrFilter  = "Image Files\0*.png;*.jpg;*.jpeg;*.bmp\0"
                     "PNG Files\0*.png\0"
                     "JPEG Files\0*.jpg;*.jpeg\0"
                     "All Files\0*.*\0";
#endif
  ofn.nFilterIndex = 1;
  ofn.Flags        = save_mode ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST;

  if (save_mode)
    return get_save_filename(&ofn);
  else
    return get_open_filename(&ofn);
}
