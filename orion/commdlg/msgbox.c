// commdlg/msgbox.c — WinAPI-style message_box() modal dialog.
//
// Implements a minimal modal dialog with a text label and one of four
// standard button combinations (OK, OK/Cancel, Yes/No, Yes/No/Cancel),
// matching the WinAPI MessageBox() calling convention.

#include <string.h>

#include "msgbox.h"
#include <orion/commctl/commctl.h>
#include <orion/user/user.h>

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

#define MB_WIN_W   240  // the height follows the wrapped message
#define MB_PAD       8
#define MB_BTN_GAP   8
#define MB_BTN_W    50
#define MB_BTN_H   BUTTON_HEIGHT

enum {
  MB_ID_TEXT = 1,
  MB_ID_OK,
  MB_ID_YES,
  MB_ID_NO,
  MB_ID_CANCEL,
};

static const form_ctrl_def_t kMsgBoxOkActions[] = {
  { .class_name = "Space", .name = "left", .h_align = LAYOUT_ALIGN_STRETCH },
  { .class_name = "Button", .id = MB_ID_OK, .size = {MB_BTN_W, MB_BTN_H},
    .flags = BUTTON_DEFAULT, .text = "OK", .name = "ok", .h_align = LAYOUT_ALIGN_START },
  { .class_name = "Space", .name = "right", .h_align = LAYOUT_ALIGN_STRETCH },
};

static const form_ctrl_def_t kMsgBoxOkCancelActions[] = {
  { .class_name = "Space", .name = "flex", .h_align = LAYOUT_ALIGN_STRETCH },
  { .class_name = "Button", .id = MB_ID_OK, .size = {MB_BTN_W, MB_BTN_H},
    .flags = BUTTON_DEFAULT, .text = "OK", .name = "ok", .h_align = LAYOUT_ALIGN_START },
  { .class_name = "Button", .id = MB_ID_CANCEL, .size = {MB_BTN_W, MB_BTN_H},
    .text = "Cancel", .name = "cancel", .h_align = LAYOUT_ALIGN_START },
};

static const form_ctrl_def_t kMsgBoxYesNoActions[] = {
  { .class_name = "Space", .name = "flex", .h_align = LAYOUT_ALIGN_STRETCH },
  { .class_name = "Button", .id = MB_ID_YES, .size = {MB_BTN_W, MB_BTN_H},
    .flags = BUTTON_DEFAULT, .text = "Yes", .name = "yes", .h_align = LAYOUT_ALIGN_START },
  { .class_name = "Button", .id = MB_ID_NO, .size = {MB_BTN_W, MB_BTN_H},
    .text = "No", .name = "no", .h_align = LAYOUT_ALIGN_START },
};

static const form_ctrl_def_t kMsgBoxYesNoCancelActions[] = {
  { .class_name = "Space", .name = "flex", .h_align = LAYOUT_ALIGN_STRETCH },
  { .class_name = "Button", .id = MB_ID_YES, .size = {MB_BTN_W, MB_BTN_H},
    .flags = BUTTON_DEFAULT, .text = "Yes", .name = "yes", .h_align = LAYOUT_ALIGN_START },
  { .class_name = "Button", .id = MB_ID_NO, .size = {MB_BTN_W, MB_BTN_H},
    .text = "No", .name = "no", .h_align = LAYOUT_ALIGN_START },
  { .class_name = "Button", .id = MB_ID_CANCEL, .size = {MB_BTN_W, MB_BTN_H},
    .text = "Cancel", .name = "cancel", .h_align = LAYOUT_ALIGN_START },
};

static const form_ctrl_def_t kMsgBoxOkChildren[] = {
  { .class_name = "Label", .id = MB_ID_TEXT, .size = {0, 0},
    .text = "", .name = "message", .h_align = LAYOUT_ALIGN_STRETCH, .v_align = LAYOUT_ALIGN_START },
  { .class_name = "StackView", .name = "actions",
    .flags = WINDOW_STACK_HORIZONTAL, .layout_spacing = MB_BTN_GAP,
    .h_align = LAYOUT_ALIGN_STRETCH, .v_align = LAYOUT_ALIGN_START,
    .children = kMsgBoxOkActions, .child_count = ARRAY_LEN(kMsgBoxOkActions) },
};

static const form_ctrl_def_t kMsgBoxOkCancelChildren[] = {
  { .class_name = "Label", .id = MB_ID_TEXT, .size = {0, 0},
    .text = "", .name = "message", .h_align = LAYOUT_ALIGN_STRETCH, .v_align = LAYOUT_ALIGN_START },
  { .class_name = "StackView", .name = "actions",
    .flags = WINDOW_STACK_HORIZONTAL, .layout_spacing = MB_BTN_GAP,
    .h_align = LAYOUT_ALIGN_STRETCH, .v_align = LAYOUT_ALIGN_START,
    .children = kMsgBoxOkCancelActions, .child_count = ARRAY_LEN(kMsgBoxOkCancelActions) },
};

static const form_ctrl_def_t kMsgBoxYesNoChildren[] = {
  { .class_name = "Label", .id = MB_ID_TEXT, .size = {0, 0},
    .text = "", .name = "message", .h_align = LAYOUT_ALIGN_STRETCH, .v_align = LAYOUT_ALIGN_START },
  { .class_name = "StackView", .name = "actions",
    .flags = WINDOW_STACK_HORIZONTAL, .layout_spacing = MB_BTN_GAP,
    .h_align = LAYOUT_ALIGN_STRETCH, .v_align = LAYOUT_ALIGN_START,
    .children = kMsgBoxYesNoActions, .child_count = ARRAY_LEN(kMsgBoxYesNoActions) },
};

static const form_ctrl_def_t kMsgBoxYesNoCancelChildren[] = {
  { .class_name = "Label", .id = MB_ID_TEXT, .size = {0, 0},
    .text = "", .name = "message", .h_align = LAYOUT_ALIGN_STRETCH, .v_align = LAYOUT_ALIGN_START },
  { .class_name = "StackView", .name = "actions",
    .flags = WINDOW_STACK_HORIZONTAL, .layout_spacing = MB_BTN_GAP,
    .h_align = LAYOUT_ALIGN_STRETCH, .v_align = LAYOUT_ALIGN_START,
    .children = kMsgBoxYesNoCancelActions, .child_count = ARRAY_LEN(kMsgBoxYesNoCancelActions) },
};

static const form_def_t kMsgBoxOkForm = {
  .name = "Message",
  .flags = WINDOW_AUTO_LAYOUT,
  .width = MB_WIN_W,
  .layout_spacing = MB_BTN_GAP,
  .padding = {MB_PAD, MB_PAD, MB_PAD, MB_PAD},
  .children = kMsgBoxOkChildren,
  .child_count = ARRAY_LEN(kMsgBoxOkChildren),
};

static const form_def_t kMsgBoxOkCancelForm = {
  .name = "Message",
  .flags = WINDOW_AUTO_LAYOUT,
  .width = MB_WIN_W,
  .layout_spacing = MB_BTN_GAP,
  .padding = {MB_PAD, MB_PAD, MB_PAD, MB_PAD},
  .children = kMsgBoxOkCancelChildren,
  .child_count = ARRAY_LEN(kMsgBoxOkCancelChildren),
};

static const form_def_t kMsgBoxYesNoForm = {
  .name = "Message",
  .flags = WINDOW_AUTO_LAYOUT,
  .width = MB_WIN_W,
  .layout_spacing = MB_BTN_GAP,
  .padding = {MB_PAD, MB_PAD, MB_PAD, MB_PAD},
  .children = kMsgBoxYesNoChildren,
  .child_count = ARRAY_LEN(kMsgBoxYesNoChildren),
};

static const form_def_t kMsgBoxYesNoCancelForm = {
  .name = "Message",
  .flags = WINDOW_AUTO_LAYOUT,
  .width = MB_WIN_W,
  .layout_spacing = MB_BTN_GAP,
  .padding = {MB_PAD, MB_PAD, MB_PAD, MB_PAD},
  .children = kMsgBoxYesNoCancelChildren,
  .child_count = ARRAY_LEN(kMsgBoxYesNoCancelChildren),
};

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

typedef struct {
  const char *text;
  uint32_t    type;
  int         result;
} mb_state_t;

// ---------------------------------------------------------------------------
// Dialog procedure
// ---------------------------------------------------------------------------

static result_t mb_proc(window_t *win, uint32_t msg,
                         uint32_t wparam, void *lparam) {
  mb_state_t *ms = (mb_state_t *)win->userdata;

  switch (msg) {
    case evCreate: {
      ms = (mb_state_t *)lparam;
      win->userdata = ms;
      set_window_item_text(win, MB_ID_TEXT, "%s", ms->text ? ms->text : "");
      return true;
    }

    case evCommand: {
      if (HIWORD(wparam) != btnClicked) return false;
      window_t *btn = (window_t *)lparam;
      if (!btn) return true;

      int code = IDCANCEL;
      if      (btn->id == MB_ID_OK)     code = IDOK;
      else if (btn->id == MB_ID_YES)    code = IDYES;
      else if (btn->id == MB_ID_NO)     code = IDNO;
      else if (btn->id == MB_ID_CANCEL) code = IDCANCEL;

      ms->result = code;
      end_dialog(win, (uint32_t)code);
      return true;
    }

    default:
      return false;
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int message_box(window_t *parent, const char *text,
                const char *caption, uint32_t type) {
  mb_state_t ms = {0};
  ms.text   = text;
  ms.type   = type;
  ms.result = IDCANCEL;

  const char *title = caption ? caption : "Message";
  const form_def_t *form = &kMsgBoxOkForm;
  switch (type & 0x0F) {
    case MB_OKCANCEL:    form = &kMsgBoxOkCancelForm; break;
    case MB_YESNO:       form = &kMsgBoxYesNoForm; break;
    case MB_YESNOCANCEL: form = &kMsgBoxYesNoCancelForm; break;
    default:             form = &kMsgBoxOkForm; break;
  }
  show_dialog_from_form(form, title, parent, mb_proc, &ms);
  return ms.result;
}
