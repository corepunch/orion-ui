#ifndef __IMAGEEDITOR_FILTER_GALLERY_PREVIEW_H__
#define __IMAGEEDITOR_FILTER_GALLERY_PREVIEW_H__

#include "../../../ui.h"

#define FG_PREVIEW_CLASS_NAME "filter_preview"

typedef struct {
  uint32_t texture;
  uint32_t program;
} fg_preview_data_t;

enum {
  fgPreviewSetData = evUser + 340,
};

result_t fg_preview_component_proc(window_t *win, uint32_t msg,
                                   uint32_t wparam, void *lparam);

#endif /* __IMAGEEDITOR_FILTER_GALLERY_PREVIEW_H__ */
