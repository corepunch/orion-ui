// VIEW: About dialog (form-based).

#include "taskmanager.h"

// ============================================================
// Form definition
// ============================================================

static const form_ctrl_def_t kAboutChildren[] = {
  { FORM_CTRL_LABEL,  -1,    {8,  8, 200, 13}, 0,             "Orion Task Manager", "lbl_title"   },
  { FORM_CTRL_LABEL,  -1,    {8, 24, 200, 13}, 0,             "Version 1.0",        "lbl_version" },
  { FORM_CTRL_LABEL,  -1,    {8, 40, 200, 13}, 0,             "CRUD demo using Orion framework.", "lbl_desc" },
  { FORM_CTRL_BUTTON, ID_OK, {80, 60, 60, 18}, BUTTON_DEFAULT, "OK",                "btn_ok"      },
};

static const form_def_t kAboutForm = {
  .name        = "About",
  .width       = 220,
  .height      = 96,
  .flags       = 0,
  .children    = kAboutChildren,
  .child_count = (int)(sizeof(kAboutChildren)/sizeof(kAboutChildren[0])),
};

// ============================================================
// Window procedure
// ============================================================

static result_t about_dlg_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
  (void)lparam;
  switch (msg) {
    case evCreate:
      return true;
    case evCommand:
      if (HIWORD(wparam) == kButtonNotificationClicked) {
        end_dialog(win, 1);
        return true;
      }
      return false;
    default:
      return false;
  }
}

// ============================================================
// Public entry point
// ============================================================

void show_about_dialog(window_t *parent) {
  show_dialog_from_form(&kAboutForm, "About Task Manager",
                        parent, about_dlg_proc, NULL);
}
