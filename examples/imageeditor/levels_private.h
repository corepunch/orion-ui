#ifndef __IMAGEEDITOR_LEVELS_PRIVATE_H__
#define __IMAGEEDITOR_LEVELS_PRIVATE_H__

#include "imageeditor.h"
#include "components/lv_cmpn.h"

typedef struct {
  canvas_doc_t *doc;
  int           layer_idx;
  bool          accepted;
  bool          preview_enabled;
  window_t     *dlg_win;
  uint8_t       black;
  uint8_t       white;
  float         gamma;
  uint8_t       out_black;
  uint8_t       out_white;
  uint32_t      hist[256];
  uint32_t      hist_max;
  window_t     *graph_win;
  window_t     *in_slider_win;
  window_t     *out_slider_win;
  window_t     *preview_win;
} lv_state_t;

#endif /* __IMAGEEDITOR_LEVELS_PRIVATE_H__ */
