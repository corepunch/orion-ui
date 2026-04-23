#include "ansi.h"

// ANSI index order (0..7 normal, 8..15 bright):
// black, red, green, yellow, blue, magenta, cyan, white.
const uint32_t kAnsi16[16] = {
  0xFF000000u, 0xFFAA0000u, 0xFF00AA00u, 0xFFAA5500u,
  0xFF0000AAu, 0xFFAA00AAu, 0xFF00AAAAu, 0xFFAAAAAAu,
  0xFF555555u, 0xFFFF5555u, 0xFF55FF55u, 0xFFFFFF55u,
  0xFF5555FFu, 0xFFFF55FFu, 0xFF55FFFFu, 0xFFFFFFFFu,
};

uint32_t ansi256_to_rgba(int idx) {
  if (idx < 0) idx = 0;
  if (idx > 255) idx = 255;

  if (idx < 16)
    return kAnsi16[idx];

  if (idx <= 231) {
    static const int steps[6] = { 0, 95, 135, 175, 215, 255 };
    int n = idx - 16;
    int r = steps[(n / 36) % 6];
    int g = steps[(n / 6) % 6];
    int b = steps[n % 6];
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
  }

  int gray = 8 + (idx - 232) * 10;
  if (gray < 0) gray = 0;
  if (gray > 255) gray = 255;
  return 0xFF000000u | ((uint32_t)gray << 16) |
         ((uint32_t)gray << 8) | (uint32_t)gray;
}

int nearest_ansi_index(uint32_t rgba) {
  int r = (int)((rgba >> 16) & 0xFF);
  int g = (int)((rgba >> 8) & 0xFF);
  int b = (int)(rgba & 0xFF);
  int best = 7;
  uint32_t best_d = 0xFFFFFFFFu;

  for (int i = 0; i < 16; i++) {
    int pr = (int)((kAnsi16[i] >> 16) & 0xFF);
    int pg = (int)((kAnsi16[i] >> 8) & 0xFF);
    int pb = (int)(kAnsi16[i] & 0xFF);
    int dr = r - pr;
    int dg = g - pg;
    int db = b - pb;
    uint32_t d = (uint32_t)(dr * dr + dg * dg + db * db);
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  return best;
}

int clamp_ansi_index(int idx) {
  if (idx < 0) return 0;
  if (idx > 15) return 15;
  return idx;
}

void ansi_apply_sgr(int code,
                    int *fg_idx, int *bg_idx,
                    int def_fg, int def_bg,
                    bool *bold) {
  if (code == 0) {
    *fg_idx = def_fg;
    *bg_idx = def_bg;
    *bold = false;
    return;
  }
  if (code == 1) {
    *bold = true;
    if (*fg_idx >= 0 && *fg_idx < 8)
      *fg_idx += 8;
    return;
  }
  if (code == 22) {
    *bold = false;
    if (*fg_idx >= 8)
      *fg_idx -= 8;
    return;
  }
  if (code == 39) {
    *fg_idx = def_fg;
    return;
  }
  if (code == 49) {
    *bg_idx = def_bg;
    return;
  }
  if (code >= 30 && code <= 37) {
    *fg_idx = code - 30;
    if (*bold) *fg_idx += 8;
    return;
  }
  if (code >= 90 && code <= 97) {
    *fg_idx = code - 90 + 8;
    return;
  }
  if (code >= 40 && code <= 47) {
    *bg_idx = code - 40;
    return;
  }
  if (code >= 100 && code <= 107) {
    *bg_idx = code - 100 + 8;
    return;
  }
}

void ansi_apply_sgr_codes(const int *codes, int n,
                          int *fg_idx, int *bg_idx,
                          int def_fg, int def_bg,
                          bool *bold) {
  for (int i = 0; i < n; i++) {
    int c = codes[i];

    if ((c == 38 || c == 48) && i + 1 < n) {
      bool set_fg = (c == 38);
      int mode = codes[i + 1];

      if (mode == 5 && i + 2 < n) {
        int pal_idx = nearest_ansi_index(ansi256_to_rgba(codes[i + 2]));
        if (set_fg)
          *fg_idx = pal_idx;
        else
          *bg_idx = pal_idx;
        i += 2;
        continue;
      }

      if (mode == 2 && i + 4 < n) {
        int r = codes[i + 2];
        int g = codes[i + 3];
        int b = codes[i + 4];
        if (r < 0) r = 0; if (r > 255) r = 255;
        if (g < 0) g = 0; if (g > 255) g = 255;
        if (b < 0) b = 0; if (b > 255) b = 255;
        uint32_t rgba = 0xFF000000u | ((uint32_t)r << 16) |
                        ((uint32_t)g << 8) | (uint32_t)b;
        int pal_idx = nearest_ansi_index(rgba);
        if (set_fg)
          *fg_idx = pal_idx;
        else
          *bg_idx = pal_idx;
        i += 4;
        continue;
      }
    }

    ansi_apply_sgr(c, fg_idx, bg_idx, def_fg, def_bg, bold);
  }
}
