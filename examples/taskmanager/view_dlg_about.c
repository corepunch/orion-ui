// VIEW: About dialog (form-based, DDX-driven).

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
  .ok_id       = ID_OK,
};

// ============================================================
// Public entry point
// ============================================================

void show_about_dialog(window_t *parent) {
  show_ddx_dialog(&kAboutForm, "About Task Manager", parent, NULL);
}
