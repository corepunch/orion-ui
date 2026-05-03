// "Image Size" dialog
//
// Lets the user scale the image contents and choose the resampling filter.

#include "imageeditor.h"

#define MAX_IMAGE_DIMENSION 16384

typedef struct {
  int w;
  int h;
  int filter;
  bool accepted;
} ir_state_t;

static const ctrl_binding_t k_ir_bindings[] = {
  DDX_TEXT(IR_ID_WIDTH, ir_state_t, w),
  DDX_TEXT(IR_ID_HEIGHT, ir_state_t, h),
  DDX_COMBO(IR_ID_FILTER, ir_state_t, filter, IMAGE_RESIZE_BILINEAR),
};

static result_t image_resize_proc(window_t *win, uint32_t msg,
                                  uint32_t wparam, void *lparam) {
  ir_state_t *s = (ir_state_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      win->userdata = lparam;
      s = (ir_state_t *)lparam;
      window_t *cb = get_window_item(win, IR_ID_FILTER);
      if (cb) {
        send_message(cb, cbAddString, 0, (void *)"Nearest Neighbor");
        send_message(cb, cbAddString, 0, (void *)"Bilinear");
      }
      dialog_push(win, s, k_ir_bindings, ARRAY_LEN(k_ir_bindings));
      return true;
    }

    case evCommand:
      if (HIWORD(wparam) == btnClicked) {
        window_t *src = (window_t *)lparam;
        if (!src) return false;
        if (src->id == IR_ID_OK) {
          dialog_pull(win, s, k_ir_bindings, ARRAY_LEN(k_ir_bindings));
          s->accepted = true;
          end_dialog(win, 1);
          return true;
        }
        if (src->id == IR_ID_CANCEL) {
          end_dialog(win, 0);
          return true;
        }
      }
      return false;

    default:
      return false;
  }
}

bool show_image_resize_dialog(window_t *parent, int *out_w, int *out_h,
                              image_resize_filter_t *out_filter) {
  if (!out_w || !out_h || !out_filter) return false;
  ir_state_t st = {
    .w = *out_w,
    .h = *out_h,
    .filter = (int)*out_filter,
    .accepted = false,
  };

  uint32_t res = show_dialog_from_form_ex(&imageeditor_image_resize_form,
                                          "Image Size",
                                          parent,
                                          WINDOW_DIALOG | WINDOW_NOTRAYBUTTON,
                                          image_resize_proc,
                                          &st);
  if (!res || !st.accepted) return false;
  if (st.w <= 0 || st.w > MAX_IMAGE_DIMENSION) return false;
  if (st.h <= 0 || st.h > MAX_IMAGE_DIMENSION) return false;
  if (st.filter < 0 || st.filter >= IMAGE_RESIZE_FILTER_COUNT)
    st.filter = IMAGE_RESIZE_BILINEAR;

  *out_w = st.w;
  *out_h = st.h;
  *out_filter = (image_resize_filter_t)st.filter;
  return true;
}
