#include "imageeditor.h"

const uint32_t k_pencil_palette[IE_PENCIL_COLORS] = {
  IE_INK_COLOR,                         IE_PAPER_COLOR,
  MAKE_COLOR(0x53, 0x60, 0x78, 0xFF), MAKE_COLOR(0xBC, 0xC5, 0xD6, 0xFF),
  MAKE_COLOR(0xCC, 0x4B, 0x52, 0xFF), MAKE_COLOR(0xF2, 0x99, 0x89, 0xFF),
  MAKE_COLOR(0xDE, 0x9E, 0x36, 0xFF), MAKE_COLOR(0xF7, 0xDF, 0x9A, 0xFF),
  MAKE_COLOR(0x86, 0x55, 0x42, 0xFF), MAKE_COLOR(0xDA, 0xAF, 0x89, 0xFF),
  MAKE_COLOR(0x3C, 0x79, 0x60, 0xFF), MAKE_COLOR(0x9C, 0xCD, 0xA3, 0xFF),
  MAKE_COLOR(0x3E, 0x70, 0xB6, 0xFF), MAKE_COLOR(0x9C, 0xCB, 0xE8, 0xFF),
  MAKE_COLOR(0x97, 0x79, 0xBE, 0xFF), MAKE_COLOR(0xE6, 0xAC, 0xCB, 0xFF),
};

const char *const k_pencil_color_names[IE_PENCIL_COLORS] = {
  "Ink", "Paper", "Slate", "Silver", "Red", "Coral", "Ochre", "Cream",
  "Brown", "Sand", "Forest", "Mint", "Blue", "Sky", "Lavender", "Rose",
};

#if IMAGEEDITOR_BW
static int pencil_unused_palette_index(const canvas_doc_t *doc) {
  bool used[256] = {0};
  used[(uint8_t)doc->ipal.transparent] = true;
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  for (int i = 0; i < doc->layer.count; i++)
    for (size_t p = 0; p < n; p++) used[doc->layer.stack[i]->pixels[p]] = true;
  for (int i = 0; doc->anim && i < doc->anim->frame_count; i++) {
    anim_frame_t *frame = doc->anim->frames[i];
    if (!frame->data || !frame->data_size) continue;
    if (frame->format != FRAME_FORMAT_INDEXED || frame->data_size < n) {
      IE_TRACE("palette scan rejected doc=%p frame=%d format=%d size=%zu",
               (void *)doc, i, frame->format, frame->data_size);
      return -1;
    }
    for (size_t p = 0; p < n; p++) used[frame->data[p]] = true;
  }
  for (int i = 0; i < 256; i++)
    if (!used[i] && (i >= doc->ipal.count || COLOR_A(doc->ipal.entries[i]) == 0)) return i;
  return -1;
}
#endif

bool cmd_pencil_color(canvas_doc_t *doc, int swatch) {
  if (!g_app || swatch < 0 || swatch >= IE_PENCIL_COLORS) return false;
  uint32_t color = k_pencil_palette[swatch];
#if IMAGEEDITOR_BW
  if (doc) {
    if (doc->command.before) return false;
    int idx = -1;
    for (int i = 0; i < doc->ipal.count; i++)
      if (i != doc->ipal.transparent && doc->ipal.entries[i] == color) { idx = i; break; }
    if (idx < 0) {
      anim_stop_playback(doc);
      idx = pencil_unused_palette_index(doc);
      if (idx < 0) {
        IE_TRACE("palette color unavailable doc=%p swatch=%d", (void *)doc, swatch);
        return false;
      }
      if (!ie_doc_begin_op(doc, "Add Palette Color")) return false;
      doc->ipal.entries[idx] = color;
      doc->ipal.count = MAX(doc->ipal.count, idx + 1);
      ie_doc_commit_op(doc, true);
    }
    g_app->fg_palette_idx = idx;
  }
#else
  (void)doc;
#endif
  g_app->fg_color = color;
  IE_TRACE("palette select doc=%p swatch=%d color=%08x", (void *)doc, swatch, color);
  if (g_app->tool_win) invalidate_window(g_app->tool_win);
  return true;
}
