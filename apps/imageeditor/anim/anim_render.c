// Animation onion textures for indexed and RGBA frames.

#include "imageeditor.h"

// Expand the compressed onion frame to RGBA in a temporary buffer, then upload
// that buffer into a RGBA GL texture.  This is the universal path that
// works for all three formats without requiring a separate FBO pass.
//
// palette: for INDEXED frames, the caller's palette (doc->ipal.entries).
//          In indexed builds the per-frame palette is not stored (the
//          working palette lives in doc->ipal), so this parameter is
//          required.  Pass NULL for non-indexed formats.
static uint8_t *anim_frame_rgba(const anim_frame_t *frame, int w, int h,
                                const uint32_t *palette) {
  if (!frame || w <= 0 || h <= 0 || (size_t)w > SIZE_MAX / 4 / (size_t)h)
    return NULL;

  size_t sz = (size_t)w * (size_t)h * 4;
  uint8_t *rgba = malloc(sz);
  if (!rgba) return NULL;

  bool ok = false;

  if (frame->data && frame->data_size > 0) {
    if (frame->format == FRAME_FORMAT_INDEXED || frame->format == FRAME_FORMAT_RLE_INDEXED) {
      // Indexed frames: expand palette indices → RGBA manually.
      // anim_frame_expand() in indexed builds does a raw memcpy of indices
      // (targeting doc->pixels, which is 1-byte/pixel), but here we always
      // need RGBA output.  Use the caller-supplied palette (doc->ipal).
      size_t npx = (size_t)w * (size_t)h;
      uint8_t *indices = malloc(npx);
      if (!indices || !anim_frame_expand_indices(frame, indices, w, h)) { free(indices); free(rgba); return NULL; }
      const uint32_t *pal = palette ? palette : frame->palette;
      for (size_t i = 0; i < npx; i++) {
        uint8_t idx = indices[i];
        uint32_t col = pal[idx];
        rgba[i*4+0] = COLOR_R(col);
        rgba[i*4+1] = COLOR_G(col);
        rgba[i*4+2] = COLOR_B(col);
        rgba[i*4+3] = COLOR_A(col);
      }
      free(indices);
      ok = true;
    } else {
      ok = anim_frame_expand(frame, rgba, w, h);
    }
  } else {
    // Empty frame — produce a transparent onion.
    memset(rgba, 0, sz);
    ok = true;
  }

  if (!ok) { free(rgba); return NULL; }
  return rgba;
}

void anim_onion_tint_rgba(uint8_t *rgba, size_t npx, uint32_t tint) {
  if (!rgba || npx == 0 || COLOR_A(tint) == 0) return;
  uint8_t tr = COLOR_R(tint), tg = COLOR_G(tint), tb = COLOR_B(tint);
  for (size_t i = 0; i < npx; i++) {
    uint8_t *p = rgba + i * 4;
    if (p[3] == 0) continue;
    int luma = (p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8;
    if (luma >= 248) { memset(p, 0, 4); continue; }
    p[0] = tr;
    p[1] = tg;
    p[2] = tb;
  }
}

bool anim_render_pencil_onion(const anim_frame_t *frame, int w, int h, uint32_t *tex) {
  if (!frame || !tex || w < 1 || h < 1) return false;
  uint8_t *coverage = malloc((size_t)w * h);
  if (!coverage) return false;
  bool ok = true;
  if (frame->cels) ok = anim_frame_expand_cel(frame, 1, coverage, w, h);
  else memset(coverage, 0, (size_t)w * h);
  uint32_t texture = ok ? R_CreateTextureR8(w, h, coverage, R_FILTER_NEAREST, R_WRAP_CLAMP) : 0;
  free(coverage);
  if (!texture) return false;
  if (*tex) R_DeleteTexture(*tex);
  *tex = texture;
  return true;
}

static bool anim_upload_onion_rgba(uint8_t *rgba, int w, int h, uint32_t *tex) {
  if (!rgba || !tex) return false;
  if (*tex == 0) {
    *tex = R_CreateTextureSRGBA8(w, h, rgba, R_FILTER_LINEAR, R_WRAP_CLAMP);
    return *tex != 0;
  }
  return R_UpdateTextureRGBA(*tex, 0, 0, w, h, rgba);
}

bool anim_render_onion(const anim_frame_t *frame, int w, int h,
                                        uint32_t *tex, const uint32_t *palette,
                                        uint32_t tint) {
  if (!tex) return false;
  uint8_t *rgba = anim_frame_rgba(frame, w, h, palette);
  if (!rgba) return false;
  anim_onion_tint_rgba(rgba, (size_t)w * (size_t)h, tint);
  bool ok = anim_upload_onion_rgba(rgba, w, h, tex);
  free(rgba);
  return ok;
}
