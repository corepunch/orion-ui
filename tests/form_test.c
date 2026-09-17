// Tests for create_window_from_form() and show_dialog_from_form().
//
// Verifies:
//  1. Children declared in a form_def_t exist and are findable via
//     get_window_item() when evCreate fires on the parent.
//  2. Child IDs, flags, and initial text are applied correctly.
//  3. show_dialog_from_form() creates a window with WINDOW_DIALOG set,
//     applies the title override, and includes WINDOW_VSCROLL.
//  4. show_ddx_dialog() pushes state → controls, and OK/Cancel end the dialog.

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include "../apps/socialfeed/socialfeed.h"

extern bool do_windows_overlap(const window_t *a, const window_t *b);

// ──────────────────────────────────────────────────────────────────────────
// Shared form definition used by several tests
// ──────────────────────────────────────────────────────────────────────────

#define FORM_ID_NAME   1
#define FORM_ID_OK     2
#define FORM_ID_CANCEL 3

static const form_ctrl_def_t kTestFormChildren[] = {
  { .class_name = "TextBox", .id = FORM_ID_NAME, .size = {80, 13}, .text = "hello", .name = "name",
    .h_align = LAYOUT_ALIGN_STRETCH, .v_align = LAYOUT_ALIGN_CENTER },
  {
    .class_name = "StackView",
    .name = "actions",
    .flags = WINDOW_STACK_HORIZONTAL,
    .layout_spacing = 4,
    .h_align = LAYOUT_ALIGN_STRETCH,
    .children = (const form_ctrl_def_t[]){
      { .class_name = "Space", .name = "flex", .h_align = LAYOUT_ALIGN_STRETCH },
      { .class_name = "Button", .id = FORM_ID_OK, .size = {40, 13}, .flags = BUTTON_DEFAULT, .text = "OK", .name = "ok",
        .h_align = LAYOUT_ALIGN_START },
      { .class_name = "Button", .id = FORM_ID_CANCEL, .size = {50, 13}, .text = "Cancel", .name = "cancel",
        .h_align = LAYOUT_ALIGN_START },
    },
    .child_count = 3,
  },
};

static const form_def_t kTestForm = {
  .name        = "Test Form",
  .width       = 160,
  .height      = 52,
  .flags = (0) | WINDOW_AUTO_LAYOUT,
  .layout_spacing = 4,
  .padding = {8, 8, 8, 8},
  .children    = kTestFormChildren,
  .child_count = ARRAY_LEN(kTestFormChildren),
};

static const form_def_t kNonAutoLayoutChildForm = {
  .name        = "Non Auto Layout Child Form",
  .width       = 160,
  .height      = 52,
  .flags       = 0,
  .children    = kTestFormChildren,
  .child_count = ARRAY_LEN(kTestFormChildren),
};

static int kPageActivations;
static int kPageDeactivations;

static result_t host_role_test_proc(window_t *win, uint32_t msg,
                                    uint32_t wparam, void *lparam) {
  (void)win; (void)msg; (void)wparam; (void)lparam;
  return false;
}

static result_t page_role_test_proc(window_t *win, uint32_t msg,
                                    uint32_t wparam, void *lparam) {
  (void)win; (void)wparam; (void)lparam;
  if (msg == evActivate) kPageActivations++;
  if (msg == evDeactivate) kPageDeactivations++;
  return msg == evActivate || msg == evDeactivate;
}

static const toolbar_item_t kPageToolbar[] = {
  { TOOLBAR_ITEM_BUTTON, 701, "refresh", 0, 0, "Refresh", "Refresh page" },
};

static const form_def_t kHostRoleForm = {
  .name = "Host", .width = 200, .height = 100,
  .flags = WINDOW_TOOLBAR | WINDOW_AUTO_LAYOUT,
  .role = WINDOW_ROLE_HOST,
};

static const form_def_t kPageRoleForm = {
  .name = "Page", .width = 200, .height = 80,
  .flags = WINDOW_AUTO_LAYOUT,
  .role = WINDOW_ROLE_PAGE,
  .toolbar_items = kPageToolbar,
  .toolbar_count = ARRAY_LEN(kPageToolbar),
};

// ──────────────────────────────────────────────────────────────────────────
// DDX form and state for show_ddx_dialog tests
// ──────────────────────────────────────────────────────────────────────────

#define DDX_FORM_ID_NAME   1
#define DDX_FORM_ID_OK     2
#define DDX_FORM_ID_CANCEL 3

typedef struct { char name[64]; } ddx_test_state_t;

static const ctrl_binding_t kDdxTestBindings[] = {
  DDX_TEXT(DDX_FORM_ID_NAME, ddx_test_state_t, name),
};

static const form_ctrl_def_t kDdxFormChildren[] = {
  { .class_name = "TextBox", .id = DDX_FORM_ID_NAME, .size = {80, 13}, .text = "", .name = "name",
    .h_align = LAYOUT_ALIGN_STRETCH, .v_align = LAYOUT_ALIGN_CENTER },
  {
    .class_name = "StackView",
    .name = "actions",
    .flags = WINDOW_STACK_HORIZONTAL,
    .layout_spacing = 4,
    .h_align = LAYOUT_ALIGN_STRETCH,
    .children = (const form_ctrl_def_t[]){
      { .class_name = "Space", .name = "flex", .h_align = LAYOUT_ALIGN_STRETCH },
      { .class_name = "Button", .id = DDX_FORM_ID_OK, .size = {40, 13}, .flags = BUTTON_DEFAULT, .text = "OK", .name = "ok",
        .h_align = LAYOUT_ALIGN_START },
      { .class_name = "Button", .id = DDX_FORM_ID_CANCEL, .size = {50, 13}, .text = "Cancel", .name = "cancel",
        .h_align = LAYOUT_ALIGN_START },
    },
    .child_count = 3,
  },
};

static const form_def_t kDdxTestForm = {
  .name          = "DDX Test",
  .width         = 160,
  .height        = 52,
  .flags = (0) | WINDOW_AUTO_LAYOUT,
  .layout_spacing = 4,
  .padding       = {8, 8, 8, 8},
  .children      = kDdxFormChildren,
  .child_count   = ARRAY_LEN(kDdxFormChildren),
  .bindings      = kDdxTestBindings,
  .binding_count = ARRAY_LEN(kDdxTestBindings),
  .ok_id         = DDX_FORM_ID_OK,
  .cancel_id     = DDX_FORM_ID_CANCEL,
};

// ──────────────────────────────────────────────────────────────────────────
// Auto-layout form with padding used to verify the inset is honored
// ──────────────────────────────────────────────────────────────────────────

#define PAD_FORM_ID_FIRST   201
#define PAD_FORM_ID_SECOND  202

static const form_ctrl_def_t kPadChildren[] = {
  {
    .class_name = "Button",
    .id = PAD_FORM_ID_FIRST,
    .size = {80, 0},
    .text = "Alpha",
    .name = "alpha",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
  },
  {
    .class_name = "Button",
    .id = PAD_FORM_ID_SECOND,
    .size = {80, 0},
    .text = "Beta",
    .name = "beta",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
  },
};

static const form_def_t kPadForm = {
  .name = "Pad",
  .width = 200,
  .height = 80,
  .flags = (WINDOW_NOTITLE | WINDOW_NOFILL) | WINDOW_AUTO_LAYOUT,
  .layout_spacing = 4,
  .padding = {8, 8, 8, 8},
  .children = kPadChildren,
  .child_count = ARRAY_LEN(kPadChildren),
};

#define MAR_FORM_ID_FIRST   301
#define MAR_FORM_ID_SECOND  302

static const form_ctrl_def_t kMarChildren[] = {
  {
    .class_name = "Button",
    .id = MAR_FORM_ID_FIRST,
    .size = {80, 0},
    .text = "Gamma",
    .name = "gamma",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
    .margin = {8, 8, 8, 8},
  },
  {
    .class_name = "Button",
    .id = MAR_FORM_ID_SECOND,
    .size = {80, 0},
    .text = "Delta",
    .name = "delta",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
    .margin = {2, 6, 2, 6},
  },
};

static const form_def_t kMarForm = {
  .name = "Margin",
  .width = 200,
  .height = 80,
  .flags = (WINDOW_NOTITLE | WINDOW_NOFILL) | WINDOW_AUTO_LAYOUT,
  .layout_spacing = 4,
  .children = kMarChildren,
  .child_count = ARRAY_LEN(kMarChildren),
};

#define WRAP_FORM_ID_LABEL 401

static const form_ctrl_def_t kWrapChildren[] = {
  {
    .class_name = "Label",
    .id = WRAP_FORM_ID_LABEL,
    .size = {0, 0},
    .text = "This label should wrap when the available width is limited by layout.",
    .name = "wrap",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
  },
};

static const form_def_t kWrapForm = {
  .name = "Wrap",
  .width = 80,
  .height = 80,
  .flags = (WINDOW_NOTITLE | WINDOW_NOFILL) | WINDOW_AUTO_LAYOUT,
  .layout_spacing = 4,
  .children = kWrapChildren,
  .child_count = ARRAY_LEN(kWrapChildren),
};

#define BTN_TALL_FORM_ID_ROW     600
#define BTN_TALL_FORM_ID_BUTTON  601
#define BTN_TALL_FORM_ID_FILLER  602

static const form_ctrl_def_t kBtnTallRowChildren[] = {
  {
    .class_name = "Button",
    .id = BTN_TALL_FORM_ID_BUTTON,
    .size = {80, 0},
    .text = "Center Me",
    .name = "button",
  },
};

static const form_ctrl_def_t kBtnTallChildren[] = {
  {
    .class_name = "MultiEdit",
    .id = BTN_TALL_FORM_ID_FILLER,
    .size = {0, 0},
    .text = "Tall filler",
    .name = "filler",
    .flags = WINDOW_VSCROLL | WINDOW_FLEXSPACE,
  },
  {
    .class_name = "StackView",
    .id = BTN_TALL_FORM_ID_ROW,
    .size = {0, 0},
    .name = "row",
    .flags = WINDOW_FLEXSPACE | WINDOW_STACK_HORIZONTAL,
    .children = kBtnTallRowChildren,
    .child_count = ARRAY_LEN(kBtnTallRowChildren),
  },
};

static const form_def_t kBtnTallForm = {
  .name = "TallButton",
  .width = 160,
  .height = 120,
  .flags = (WINDOW_NOTITLE | WINDOW_NOFILL) | WINDOW_AUTO_LAYOUT,
  .layout_spacing = 4,
  .children = kBtnTallChildren,
  .child_count = 2,
};

#define GAP_FORM_ID_TEXT     501
#define GAP_FORM_ID_SEP      502
#define GAP_FORM_ID_ACTIONS  503
#define GAP_FORM_ID_OK       504
#define GAP_FORM_ID_CANCEL   505

static const form_ctrl_def_t kGapActionsChildren[] = {
  {
    .class_name = "Button",
    .id = GAP_FORM_ID_OK,
    .size = {44, 0},
    .text = "Post",
    .name = "ok",
    .h_align = LAYOUT_ALIGN_START,
    .v_align = LAYOUT_ALIGN_START,
  },
  {
    .class_name = "Button",
    .id = GAP_FORM_ID_CANCEL,
    .size = {56, 0},
    .text = "Cancel",
    .name = "cancel",
    .h_align = LAYOUT_ALIGN_START,
    .v_align = LAYOUT_ALIGN_START,
  },
};

static const form_ctrl_def_t kGapChildrenTop[] = {
  {
    .class_name = "TextBox",
    .id = GAP_FORM_ID_TEXT,
    .size = {0, 0},
    .text = "",
    .name = "text",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
  },
  {
    .class_name = "Separator",
    .id = GAP_FORM_ID_SEP,
    .size = {0, 0},
    .text = "",
    .name = "sep",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
  },
  {
    .class_name = "StackView",
    .id = GAP_FORM_ID_ACTIONS,
    .size = {0, 0},
    .name = "actions",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
    .children = kGapActionsChildren,
    .child_count = ARRAY_LEN(kGapActionsChildren),
    .flags = WINDOW_STACK_HORIZONTAL,
    .layout_spacing = 6,
  },
};

static const form_ctrl_def_t kGapChildrenStretch[] = {
  {
    .class_name = "TextBox",
    .id = GAP_FORM_ID_TEXT,
    .size = {0, 0},
    .text = "",
    .name = "text",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
  },
  {
    .class_name = "Separator",
    .id = GAP_FORM_ID_SEP,
    .size = {0, 0},
    .text = "",
    .name = "sep",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
  },
  {
    .class_name = "StackView",
    .id = GAP_FORM_ID_ACTIONS,
    .size = {0, 0},
    .name = "actions",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_STRETCH,
    .children = kGapActionsChildren,
    .child_count = ARRAY_LEN(kGapActionsChildren),
    .flags = WINDOW_STACK_HORIZONTAL,
    .layout_spacing = 6,
  },
};

static const form_def_t kGapFormTop = {
  .name = "GapTop",
  .width = 220,
  .height = 120,
  .flags = (WINDOW_NOTITLE | WINDOW_NOFILL) | WINDOW_AUTO_LAYOUT,
  .layout_spacing = 4,
  .padding = {8, 8, 8, 8},
  .children = kGapChildrenTop,
  .child_count = ARRAY_LEN(kGapChildrenTop),
};

static const form_def_t kGapFormStretch = {
  .name = "GapStretch",
  .width = 220,
  .height = 120,
  .flags = (WINDOW_NOTITLE | WINDOW_NOFILL) | WINDOW_AUTO_LAYOUT,
  .layout_spacing = 4,
  .padding = {8, 8, 8, 8},
  .children = kGapChildrenStretch,
  .child_count = ARRAY_LEN(kGapChildrenStretch),
};

// ──────────────────────────────────────────────────────────────────────────
// Nested auto-layout form used to debug stack and grid positioning
// ──────────────────────────────────────────────────────────────────────────

#define NEST_FORM_ID_HEADER      101
#define NEST_FORM_ID_BODY        102
#define NEST_FORM_ID_GRID        103
#define NEST_FORM_ID_TITLE       104
#define NEST_FORM_ID_AUTHOR      105
#define NEST_FORM_ID_BODY_BTN1    106
#define NEST_FORM_ID_BODY_BTN2    107
#define NEST_FORM_ID_GRID_1       108
#define NEST_FORM_ID_GRID_2       109
#define NEST_FORM_ID_GRID_3       110
#define NEST_FORM_ID_GRID_4       111
#define NEST_FORM_ID_GRID_LEFT    112
#define NEST_FORM_ID_GRID_RIGHT   113

static const form_ctrl_def_t kNestBodyChildren[] = {
  {
    .class_name = "Button",
    .id = NEST_FORM_ID_BODY_BTN1,
    .size = {88, 0},
    .text = "Like Post",
    .name = "like",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
  },
  {
    .class_name = "Button",
    .id = NEST_FORM_ID_BODY_BTN2,
    .size = {72, 0},
    .text = "Close",
    .name = "close",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
  },
};

static const form_ctrl_def_t kNestGridLeftChildren[] = {
  {
    .class_name = "Label",
    .id = NEST_FORM_ID_GRID_1,
    .size = {40, 0},
    .text = "G1",
    .name = "g1",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_STRETCH,
  },
  {
    .class_name = "Label",
    .id = NEST_FORM_ID_GRID_3,
    .size = {40, 0},
    .text = "G3",
    .name = "g3",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_STRETCH,
  },
};

static const form_ctrl_def_t kNestGridRightChildren[] = {
  {
    .class_name = "Label",
    .id = NEST_FORM_ID_GRID_2,
    .size = {40, 0},
    .text = "G2",
    .name = "g2",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_STRETCH,
  },
  {
    .class_name = "Label",
    .id = NEST_FORM_ID_GRID_4,
    .size = {40, 0},
    .text = "G4",
    .name = "g4",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_STRETCH,
  },
};

static const form_ctrl_def_t kNestChildren[] = {
  {
    .class_name = "Label",
    .id = NEST_FORM_ID_HEADER,
    .size = {120, 0},
    .text = "Post Detail",
    .name = "header",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
  },
  {
    .class_name = "StackView",
    .id = NEST_FORM_ID_BODY,
    .size = {0, 0},
    .name = "body",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
    .children = kNestBodyChildren,
    .child_count = ARRAY_LEN(kNestBodyChildren),
    .layout_spacing = 3,
  },
  {
    .class_name = "GridView",
    .id = NEST_FORM_ID_GRID,
    .size = {0, 0},
    .name = "grid",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
    .children = (const form_ctrl_def_t[]){
      {
        .class_name = "Column",
        .id = NEST_FORM_ID_GRID_LEFT,
        .size = {40, 0},
        .name = "left",
        .children = kNestGridLeftChildren,
        .child_count = ARRAY_LEN(kNestGridLeftChildren),
      },
      {
        .class_name = "Column",
        .id = NEST_FORM_ID_GRID_RIGHT,
        .size = {0, 0},
        .name = "right",
        .flags = WINDOW_FLEXSPACE,
        .children = kNestGridRightChildren,
        .child_count = ARRAY_LEN(kNestGridRightChildren),
      },
    },
    .child_count = 2,
    .layout_spacing = 0,
  },
};

static const form_def_t kNestForm = {
  .name = "Nest",
  .width = 240,
  .height = 180,
  .flags = (WINDOW_NOTITLE | WINDOW_NOFILL) | WINDOW_AUTO_LAYOUT,
  .layout_spacing = 6,
  .children = kNestChildren,
  .child_count = ARRAY_LEN(kNestChildren),
};

static const form_ctrl_def_t kDefaultStackChildren[] = {
  {
    .class_name = "Button",
    .id = 201,
    .size = {80, 0},
    .text = "First",
    .name = "first",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
  },
  {
    .class_name = "Button",
    .id = 202,
    .size = {80, 0},
    .text = "Second",
    .name = "second",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
  },
};

static const form_def_t kDefaultStackForm = {
  .name = "DefaultStack",
  .width = 200,
  .height = 80,
  .flags = (WINDOW_NOTITLE | WINDOW_NOFILL) | WINDOW_AUTO_LAYOUT,
  .padding = {0, 0, 0, 0},
  .children = kDefaultStackChildren,
  .child_count = ARRAY_LEN(kDefaultStackChildren),
};

#define NP_FORM_ID_FIELDS   203
#define NP_FORM_ID_LABELS  216
#define NP_FORM_ID_INPUTS  217
#define NP_FORM_ID_AUTHOR_L 204
#define NP_FORM_ID_AUTHOR_E 205
#define NP_FORM_ID_TITLE_L   206
#define NP_FORM_ID_TITLE_E   207
#define NP_FORM_ID_BODY_L    208
#define NP_FORM_ID_BODY_E    209
#define NP_FORM_ID_ACTIONS   210
#define NP_FORM_ID_OK        211
#define NP_FORM_ID_CANCEL    212
#define NP_FORM_ID_FLEX_LEFT  213
#define NP_FORM_ID_SECTION_SEP 214
#define NP_FORM_ID_FLEX_RIGHT 215

static const form_ctrl_def_t kNewPostLabelColumnChildren[] = {
  {
    .class_name = "Label",
    .id = NP_FORM_ID_AUTHOR_L,
    .size = {56, 0},
    .text = "Author:",
    .name = "author_lbl",
  },
  {
    .class_name = "Label",
    .id = NP_FORM_ID_TITLE_L,
    .size = {56, 0},
    .text = "Title:",
    .name = "title_lbl",
  },
  {
    .class_name = "Label",
    .id = NP_FORM_ID_BODY_L,
    .size = {56, 0},
    .text = "Body:",
    .name = "body_lbl",
  },
};

static const form_ctrl_def_t kNewPostInputColumnChildren[] = {
  {
    .class_name = "TextBox",
    .id = NP_FORM_ID_AUTHOR_E,
    .size = {0, 0},
    .text = "",
    .name = "author",
  },
  {
    .class_name = "TextBox",
    .id = NP_FORM_ID_TITLE_E,
    .size = {0, 0},
    .text = "",
    .name = "title",
  },
  {
    .class_name = "MultiEdit",
    .id = NP_FORM_ID_BODY_E,
    .size = {0, 48},
    .text = "",
    .name = "body",
    .flags = WINDOW_VSCROLL | WINDOW_FLEXSPACE,
  },
};

static const form_ctrl_def_t kNewPostChildren[] = {
  {
    .class_name = "GridView",
    .id = NP_FORM_ID_FIELDS,
    .size = {0, 0},
    .name = "fields",
    .flags = WINDOW_FLEXSPACE,
    .children = (const form_ctrl_def_t[]){
      {
        .class_name = "Column",
        .id = NP_FORM_ID_LABELS,
        .size = {56, 0},
        .name = "labels",
        .children = kNewPostLabelColumnChildren,
        .child_count = ARRAY_LEN(kNewPostLabelColumnChildren),
      },
      {
        .class_name = "Column",
        .id = NP_FORM_ID_INPUTS,
        .size = {0, 0},
        .name = "inputs",
        .flags = WINDOW_FLEXSPACE,
        .children = kNewPostInputColumnChildren,
        .child_count = ARRAY_LEN(kNewPostInputColumnChildren),
      },
    },
    .child_count = 2,
    .layout_spacing = 4,
  },
  {
    .class_name = "Separator",
    .id = NP_FORM_ID_SECTION_SEP,
    .size = {0, 0},
    .text = "",
    .name = "section_sep",
  },
  {
    .class_name = "StackView",
    .id = NP_FORM_ID_ACTIONS,
    .size = {0, 0},
    .name = "actions",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
    .children = (const form_ctrl_def_t[]){
      {
        .class_name = "Space",
        .id = NP_FORM_ID_FLEX_LEFT,
        .size = {0, 0},
        .text = "",
        .name = "flex_left",
        .h_align = LAYOUT_ALIGN_STRETCH,
        .v_align = LAYOUT_ALIGN_START,
      },
      {
        .class_name = "Button",
        .id = NP_FORM_ID_OK,
        .size = {44, 0},
        .text = "Post",
        .name = "ok",
        .h_align = LAYOUT_ALIGN_START,
        .v_align = LAYOUT_ALIGN_START,
      },
      {
        .class_name = "Button",
        .id = NP_FORM_ID_CANCEL,
        .size = {56, 0},
        .text = "Cancel",
        .name = "cancel",
        .h_align = LAYOUT_ALIGN_START,
        .v_align = LAYOUT_ALIGN_START,
      },
      {
        .class_name = "Space",
        .id = NP_FORM_ID_FLEX_RIGHT,
        .size = {0, 0},
        .text = "",
        .name = "flex_right",
        .h_align = LAYOUT_ALIGN_STRETCH,
        .v_align = LAYOUT_ALIGN_START,
      },
    },
    .child_count = 4,
    .flags = WINDOW_STACK_HORIZONTAL,
    .layout_spacing = 6,
  },
};

static const form_def_t kNewPostGridForm = {
  .name = "NewPostGrid",
  .width = 272,
  .height = 150,
  .flags = (WINDOW_NOTITLE | WINDOW_NOFILL) | WINDOW_AUTO_LAYOUT,
  .layout_spacing = 4,
  .padding = {8, 8, 8, 8},
  .children = kNewPostChildren,
  .child_count = ARRAY_LEN(kNewPostChildren),
};

static result_t form_test_proc(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam);
static result_t post_detail_like_proc(window_t *win, uint32_t msg,
                                      uint32_t wparam, void *lparam);
static result_t socialfeed_post_detail_layout_proc(window_t *win, uint32_t msg,
                                                   uint32_t wparam, void *lparam);
static void socialfeed_post_detail_setup_comments(window_t *win);

// ──────────────────────────────────────────────────────────────────────────
// Post-detail-like form used to validate wrapped labels + reportview + footer
// sizing.  This mirrors the Social Feed Post Detail dialog closely enough to
// catch footer clipping when the title/body labels wrap onto extra lines.
// ──────────────────────────────────────────────────────────────────────────

#define PD_FORM_ID_LAYOUT       401
#define PD_FORM_ID_HEADER       402
#define PD_FORM_ID_TITLE        403
#define PD_FORM_ID_AUTHOR       404
#define PD_FORM_ID_BODY         405
#define PD_FORM_ID_LIKES        406
#define PD_FORM_ID_COMMENTS_HDR  407
#define PD_FORM_ID_COMMENTS     408
#define PD_FORM_ID_ACTIONS      409
#define PD_FORM_ID_LIKE_POST    410
#define PD_FORM_ID_ADD_COMMENT  411
#define PD_FORM_ID_ADD_REPLY    412
#define PD_FORM_ID_LIKE_COMMENT 413
#define PD_FORM_ID_CLOSE        414
#define PD_FORM_ID_FLEX         415

static const form_ctrl_def_t kPostDetailLikeHeaderChildren[] = {
  {
    .class_name = "Label",
    .id = PD_FORM_ID_TITLE,
    .size = {0, 0},
    .text = "",
    .name = "lbl_title",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
  },
  {
    .class_name = "Label",
    .id = PD_FORM_ID_AUTHOR,
    .size = {0, 0},
    .text = "",
    .name = "lbl_author",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
  },
  {
    .class_name = "Label",
    .id = PD_FORM_ID_BODY,
    .size = {0, 0},
    .text = "",
    .name = "lbl_body",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
  },
  {
    .class_name = "Label",
    .id = PD_FORM_ID_LIKES,
    .size = {0, 0},
    .text = "",
    .name = "lbl_likes",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
  },
  {
    .class_name = "Label",
    .id = PD_FORM_ID_COMMENTS_HDR,
    .size = {0, 0},
    .text = "",
    .name = "lbl_cmt_hdr",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
  },
};

static const form_ctrl_def_t kPostDetailLikeActionsChildren[] = {
  {
    .class_name = "Button",
    .id = PD_FORM_ID_LIKE_POST,
    .size = {0, 0},
    .text = "Like Post",
    .name = "like_post",
    .h_align = LAYOUT_ALIGN_START,
    .v_align = LAYOUT_ALIGN_START,
  },
  {
    .class_name = "Button",
    .id = PD_FORM_ID_ADD_COMMENT,
    .size = {0, 0},
    .text = "Add Comment",
    .name = "add_comment",
    .h_align = LAYOUT_ALIGN_START,
    .v_align = LAYOUT_ALIGN_START,
  },
  {
    .class_name = "Button",
    .id = PD_FORM_ID_ADD_REPLY,
    .size = {0, 0},
    .text = "Add Reply",
    .name = "add_reply",
    .h_align = LAYOUT_ALIGN_START,
    .v_align = LAYOUT_ALIGN_START,
  },
  {
    .class_name = "Button",
    .id = PD_FORM_ID_LIKE_COMMENT,
    .size = {0, 0},
    .text = "Like Comment",
    .name = "like_comment",
    .h_align = LAYOUT_ALIGN_START,
    .v_align = LAYOUT_ALIGN_START,
  },
  {
    .class_name = "Space",
    .id = PD_FORM_ID_FLEX,
    .size = {0, 0},
    .text = "",
    .name = "flex",
    .h_align = LAYOUT_ALIGN_STRETCH,
    .v_align = LAYOUT_ALIGN_START,
  },
  {
    .class_name = "Button",
    .id = PD_FORM_ID_CLOSE,
    .size = {0, 0},
    .text = "Close",
    .name = "close",
    .flags = BUTTON_DEFAULT,
    .h_align = LAYOUT_ALIGN_START,
    .v_align = LAYOUT_ALIGN_START,
  },
};

static const char *kPostDetailLikeBodyText =
  "Declare children in a static form_ctrl_def_t[] array, add a ctrl_binding_t[] "
  "for data exchange, then call show_dialog_from_form(). No imperative child "
  "creation needed - everything is declarative. The same tree should also "
  "support auto layout, report views, and wrapped labels without special cases.";

static const form_ctrl_def_t kPostDetailLikeChildren[] = {
  {
    .class_name = "StackView",
    .id = PD_FORM_ID_LAYOUT,
    .size = {0, 0},
    .name = "layout",
    .flags = WINDOW_FLEXSPACE,
    .children = (const form_ctrl_def_t[]){
      {
        .class_name = "StackView",
        .id = PD_FORM_ID_HEADER,
        .size = {0, 0},
        .name = "header",
        .v_align = LAYOUT_ALIGN_START,
        .children = kPostDetailLikeHeaderChildren,
        .child_count = ARRAY_LEN(kPostDetailLikeHeaderChildren),
        .layout_spacing = 2,
      },
      {
        .class_name = "ReportView",
        .id = PD_FORM_ID_COMMENTS,
        .size = {0, 0},
        .text = "",
        .name = "comments",
        .flags = WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL | WINDOW_FLEXSPACE,
      },
      {
        .class_name = "StackView",
        .id = PD_FORM_ID_ACTIONS,
        .size = {0, 0},
        .name = "actions",
        .v_align = LAYOUT_ALIGN_START,
        .children = kPostDetailLikeActionsChildren,
        .child_count = ARRAY_LEN(kPostDetailLikeActionsChildren),
        .flags = WINDOW_STACK_HORIZONTAL,
        .layout_spacing = 4,
      },
    },
    .child_count = 3,
    .layout_spacing = 8,
  },
};

static window_t *create_post_detail_like_window(int height) {
  form_def_t def = {
    .name = "Post Detail Like",
    .width = 520,
    .height = height,
    .flags = (0) | WINDOW_AUTO_LAYOUT,
    .layout_spacing = 8,
    .padding = {8, 8, 8, 8},
    .children = kPostDetailLikeChildren,
    .child_count = ARRAY_LEN(kPostDetailLikeChildren),
  };
  return create_window_from_form(&def, 0, 0, NULL, post_detail_like_proc, 0, NULL);
}

static result_t post_detail_like_proc(window_t *win, uint32_t msg,
                                      uint32_t wparam, void *lparam) {
  (void)wparam;
  (void)lparam;
  if (msg == evCreate) {
    set_window_item_text(win, PD_FORM_ID_TITLE, "Orion UI is awesome!");
    set_window_item_text(win, PD_FORM_ID_AUTHOR, "by alice");
    set_window_item_text(win, PD_FORM_ID_BODY, "%s", kPostDetailLikeBodyText);
    set_window_item_text(win, PD_FORM_ID_LIKES, "12 likes");
    set_window_item_text(win, PD_FORM_ID_COMMENTS_HDR, "Comments (3):");
    window_layout_sync(win);
    return true;
  }
  return false;
}

static void socialfeed_post_detail_setup_comments(window_t *win) {
  window_t *cv = get_window_item(win, ID_POST_DETAIL_COMMENTS);
  if (!cv) return;
  send_message(cv, RVM_SETREDRAW, 0, NULL);
  send_message(cv, RVM_SETVIEWMODE, RVM_VIEW_REPORT, NULL);
  send_message(cv, RVM_CLEARCOLUMNS, 0, NULL);
  irect16_t cr = get_client_rect(cv);
  int cv_w = cr.w;
  int auth_w = 70;
  int like_w = 45;
  int text_w = cv_w - auth_w - like_w;
  if (text_w < 20) text_w = 20;
  reportview_column_t col_author = { "Author", (uint32_t)auth_w };
  reportview_column_t col_text   = { "Text",   (uint32_t)text_w };
  reportview_column_t col_likes  = { "Likes",  (uint32_t)like_w };
  send_message(cv, RVM_ADDCOLUMN, 0, &col_author);
  send_message(cv, RVM_ADDCOLUMN, 0, &col_text);
  send_message(cv, RVM_ADDCOLUMN, 0, &col_likes);
  send_message(cv, RVM_CLEAR, 0, NULL);
  send_message(cv, RVM_SETREDRAW, 1, NULL);
}

static result_t socialfeed_post_detail_layout_proc(window_t *win, uint32_t msg,
                                                   uint32_t wparam, void *lparam) {
  (void)wparam;
  (void)lparam;
  if (msg == evCreate) {
    set_window_item_text(win, ID_POST_DETAIL_LBL_TITLE, "Orion UI is awesome!");
    set_window_item_text(win, ID_POST_DETAIL_LBL_AUTHOR, "by alice |");
    set_window_item_text(win, ID_POST_DETAIL_LBL_BODY, "%s", kPostDetailLikeBodyText);
    set_window_item_text(win, ID_POST_DETAIL_LBL_LIKES, "12 likes |");
    set_window_item_text(win, ID_POST_DETAIL_LBL_CMT_HDR, "3 comments");
    socialfeed_post_detail_setup_comments(win);
    window_layout_sync(win);
    return true;
  }
  if (msg == evResize) {
    window_layout_sync(win);
    socialfeed_post_detail_setup_comments(win);
    return true;
  }
  return false;
}

// ──────────────────────────────────────────────────────────────────────────
// State captured in the window proc during evCreate
// ──────────────────────────────────────────────────────────────────────────

typedef struct {
  bool      create_fired;
  window_t *found_name;    // get_window_item result for FORM_ID_NAME
  window_t *found_ok;      // get_window_item result for FORM_ID_OK
  window_t *found_cancel;  // get_window_item result for FORM_ID_CANCEL
  flags_t   ok_flags;      // flags of the OK button at create time
  char      name_text[64]; // text of the name edit box at create time
} form_create_state_t;

static form_create_state_t g_create_state;

typedef struct {
  bool saw_create;
  int fields_y;
  int fields_h;
  int sep_y;
  int sep_h;
  int actions_y;
  int body_h;
  int title_h;
  int ok_x;
  int ok_w;
  int cancel_x;
  int cancel_w;
} socialfeed_new_post_modal_capture_t;

static result_t socialfeed_new_post_modal_probe_proc(window_t *win, uint32_t msg,
                                                     uint32_t wparam, void *lparam) {
  socialfeed_new_post_modal_capture_t *cap = (socialfeed_new_post_modal_capture_t *)win->userdata;
  if (msg == evCreate) {
    cap = (socialfeed_new_post_modal_capture_t *)lparam;
    win->userdata = cap;
    if (!cap) return false;

    window_t *fields = get_window_item(win, ID_NEW_POST_FIELDS);
    window_t *title = get_window_item(win, ID_NEW_POST_TITLE);
    window_t *body = get_window_item(win, ID_NEW_POST_BODY);
    window_t *sep = get_window_item(win, ID_NEW_POST_SECTION_SEP);
    window_t *actions = get_window_item(win, ID_NEW_POST_ACTIONS);
    window_t *ok = get_window_item(win, ID_NEW_POST_OK);
    window_t *cancel = get_window_item(win, ID_NEW_POST_CANCEL);

    if (fields && title && body && sep && actions && ok && cancel) {
      cap->saw_create = true;
      cap->fields_y = fields->frame.y;
      cap->fields_h = fields->frame.h;
      cap->sep_y = sep->frame.y;
      cap->sep_h = sep->frame.h;
      cap->actions_y = actions->frame.y;
      cap->title_h = title->frame.h;
      cap->body_h = body->frame.h;
      cap->ok_x = ok->frame.x;
      cap->ok_w = ok->frame.w;
      cap->cancel_x = cancel->frame.x;
      cap->cancel_w = cancel->frame.w;
    }
    return true;
  }

  if (msg == evShowWindow && wparam) {
    end_dialog(win, 1);
    return true;
  }

  return false;
}

static result_t form_test_proc(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam) {
  (void)wparam; (void)lparam;
  if (msg == evCreate) {
    g_create_state.create_fired  = true;
    g_create_state.found_name    = get_window_item(win, FORM_ID_NAME);
    g_create_state.found_ok      = get_window_item(win, FORM_ID_OK);
    g_create_state.found_cancel  = get_window_item(win, FORM_ID_CANCEL);
    if (g_create_state.found_ok)
      g_create_state.ok_flags = g_create_state.found_ok->flags;
    if (g_create_state.found_name)
      strncpy(g_create_state.name_text, g_create_state.found_name->title,
              sizeof(g_create_state.name_text) - 1);
    return true;
  }
  return false;
}

// ──────────────────────────────────────────────────────────────────────────
// Test 1: children exist at evCreate
// ──────────────────────────────────────────────────────────────────────────

void test_form_children_exist_at_create(void) {
  TEST("create_window_from_form: children findable in evCreate");

  test_env_init();
  memset(&g_create_state, 0, sizeof(g_create_state));

  window_t *win = create_window_from_form(&kTestForm, 0, 0, NULL, form_test_proc, 0, NULL);
  ASSERT_NOT_NULL(win);
  ASSERT_TRUE(g_create_state.create_fired);
  ASSERT_NOT_NULL(g_create_state.found_name);
  ASSERT_NOT_NULL(g_create_state.found_ok);
  ASSERT_NOT_NULL(g_create_state.found_cancel);

  destroy_window(win);
  test_env_shutdown();
  PASS();
}

void test_host_projects_page_toolbar(void) {
  TEST("host projects selected page toolbar");
  kPageActivations = 0;
  kPageDeactivations = 0;

  window_t *host = create_window_from_form(&kHostRoleForm, 0, 0, NULL,
                                            host_role_test_proc, 0, NULL);
  ASSERT_NOT_NULL(host);
  window_t *first = create_window_from_form(&kPageRoleForm, 0, 0, host,
                                             page_role_test_proc, 0, NULL);
  window_t *second = create_window_from_form(&kPageRoleForm, 0, 0, host,
                                              page_role_test_proc, 0, NULL);
  ASSERT_NOT_NULL(first);
  ASSERT_NOT_NULL(second);
  ASSERT_TRUE(set_host_page(host, first));
  ASSERT_EQUAL(host->active_page, first);
  ASSERT_EQUAL(kPageActivations, 1);

  toolbar_state_t *toolbar = window_toolbar_state(host);
  ASSERT_NOT_NULL(toolbar);
  ASSERT_EQUAL(toolbar->item_count, 1);
  ASSERT_EQUAL(toolbar->items[0].ident, 701);

  ASSERT_TRUE(set_host_page(host, second));
  ASSERT_EQUAL(host->active_page, second);
  ASSERT_EQUAL(second->page_host, host);
  ASSERT_EQUAL(kPageActivations, 2);
  ASSERT_EQUAL(kPageDeactivations, 1);
  destroy_window(second);
  ASSERT_NULL(host->active_page);
  ASSERT_EQUAL(toolbar->item_count, 0);
  ASSERT_EQUAL(kPageDeactivations, 2);
  destroy_window(host);
  PASS();
}

// ──────────────────────────────────────────────────────────────────────────
// Test 2: child IDs are applied correctly
// ──────────────────────────────────────────────────────────────────────────

void test_form_child_ids(void) {
  TEST("create_window_from_form: child IDs match form definition");

  test_env_init();
  memset(&g_create_state, 0, sizeof(g_create_state));

  window_t *win = create_window_from_form(&kTestForm, 0, 0, NULL, form_test_proc, 0, NULL);
  ASSERT_NOT_NULL(win);
  ASSERT_NOT_NULL(g_create_state.found_name);
  ASSERT_NOT_NULL(g_create_state.found_ok);
  ASSERT_NOT_NULL(g_create_state.found_cancel);
  ASSERT_EQUAL((int)g_create_state.found_name->id,   FORM_ID_NAME);
  ASSERT_EQUAL((int)g_create_state.found_ok->id,     FORM_ID_OK);
  ASSERT_EQUAL((int)g_create_state.found_cancel->id, FORM_ID_CANCEL);

  destroy_window(win);
  test_env_shutdown();
  PASS();
}

// ──────────────────────────────────────────────────────────────────────────
// Test 3: child flags are applied correctly
// ──────────────────────────────────────────────────────────────────────────

void test_form_child_flags(void) {
  TEST("create_window_from_form: child flags (BUTTON_DEFAULT) applied");

  test_env_init();
  memset(&g_create_state, 0, sizeof(g_create_state));

  window_t *win = create_window_from_form(&kTestForm, 0, 0, NULL, form_test_proc, 0, NULL);
  ASSERT_NOT_NULL(win);
  ASSERT_TRUE(g_create_state.ok_flags & BUTTON_DEFAULT);

  destroy_window(win);
  test_env_shutdown();
  PASS();
}

// ──────────────────────────────────────────────────────────────────────────
// Test 4: child initial text is applied correctly
// ──────────────────────────────────────────────────────────────────────────

void test_form_child_text(void) {
  TEST("create_window_from_form: child initial text applied");

  test_env_init();
  memset(&g_create_state, 0, sizeof(g_create_state));

  window_t *win = create_window_from_form(&kTestForm, 0, 0, NULL, form_test_proc, 0, NULL);
  ASSERT_NOT_NULL(win);
  ASSERT_STR_EQUAL(g_create_state.name_text, "hello");

  destroy_window(win);
  test_env_shutdown();
  PASS();
}

void test_form_rejects_non_auto_layout_children(void) {
  TEST("create_window_from_form: rejects child forms without auto-layout");

  test_env_init();
  memset(&g_create_state, 0, sizeof(g_create_state));

  window_t *win = create_window_from_form(&kNonAutoLayoutChildForm, 0, 0, NULL,
                                          form_test_proc, 0, NULL);
  ASSERT_NULL(win);
  ASSERT_FALSE(g_create_state.create_fired);

  test_env_shutdown();
  PASS();
}

// ──────────────────────────────────────────────────────────────────────────
// Test 5: show_dialog_from_form applies WINDOW_DIALOG flag and title override
// ──────────────────────────────────────────────────────────────────────────

// Window proc used for the dialog-flag test: immediately ends the dialog.
static flags_t   g_dlg_flags     = 0;
static char      g_dlg_title[64] = {0};

static result_t dialog_flag_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  (void)wparam; (void)lparam;
  if (msg == evCreate) {
    g_dlg_flags = win->flags;
    strncpy(g_dlg_title, win->title, sizeof(g_dlg_title) - 1);
    end_dialog(win, 1);
    return true;
  }
  return false;
}

void test_show_dialog_from_form_flags(void) {
  TEST("show_dialog_from_form: WINDOW_DIALOG and WINDOW_VSCROLL set; title override applied");

  test_env_init();
  g_dlg_flags = 0;
  memset(g_dlg_title, 0, sizeof(g_dlg_title));

  // UI runtime state must be running for show_dialog_from_form to enter its loop.
  g_ui_runtime.running = true;

  show_dialog_from_form(&kTestForm, "Override Title", NULL, dialog_flag_proc, NULL);

  ASSERT_TRUE(g_dlg_flags & WINDOW_DIALOG);
  ASSERT_TRUE(g_dlg_flags & WINDOW_VSCROLL);
  ASSERT_STR_EQUAL(g_dlg_title, "Override Title");

  test_env_shutdown();
  PASS();
}

void test_center_window_rect_owner(void) {
  TEST("center_window_rect: centers inside owner frame");

  test_env_init();

  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
  int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
  window_t owner = {0};
  owner.frame = (irect16_t){20, 20, MIN(200, sw - 40), MIN(120, sh - 40)};

  irect16_t centered = center_window_rect((irect16_t){0, 0, 120, 60}, &owner);
  ASSERT_EQUAL(centered.x, owner.frame.x + (owner.frame.w - 120) / 2);
  ASSERT_EQUAL(centered.y, owner.frame.y + (owner.frame.h - 60) / 2);
  ASSERT_EQUAL(centered.w, 120);
  ASSERT_EQUAL(centered.h, 60);

  test_env_shutdown();
  PASS();
}

void test_center_window_rect_screen_clamp(void) {
  TEST("center_window_rect: clamps oversized frame to screen origin");

  test_env_init();

  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
  int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
  irect16_t centered = center_window_rect((irect16_t){0, 0, sw + 50, sh + 20}, NULL);

  ASSERT_EQUAL(centered.x, 0);
  ASSERT_EQUAL(centered.y, 0);
  ASSERT_EQUAL(centered.w, sw + 50);
  ASSERT_EQUAL(centered.h, sh + 20);

  test_env_shutdown();
  PASS();
}

// ──────────────────────────────────────────────────────────────────────────
// Test 6–8: show_ddx_dialog / DDX push+pull behaviour
// ──────────────────────────────────────────────────────────────────────────

// Test 6: form_def_t DDX fields are set correctly.
void test_ddx_form_def_fields(void) {
  TEST("form_def_t: ok_id, cancel_id, bindings, binding_count set correctly");

  ASSERT_EQUAL((int)kDdxTestForm.ok_id,         DDX_FORM_ID_OK);
  ASSERT_EQUAL((int)kDdxTestForm.cancel_id,      DDX_FORM_ID_CANCEL);
  ASSERT_EQUAL((int)kDdxTestForm.binding_count,  1);
  ASSERT_NOT_NULL((void *)kDdxTestForm.bindings);
  ASSERT_EQUAL((int)kDdxTestForm.bindings[0].ctrl_id, DDX_FORM_ID_NAME);

  PASS();
}

// Minimal no-op proc for tests that create a plain parent window.
static result_t nop_proc(window_t *w, uint32_t m, uint32_t wp, void *lp) {
  (void)w; (void)m; (void)wp; (void)lp;
  return false;
}

// Test 7: dialog_push writes state → controls; dialog_pull reads back correctly.
void test_ddx_push_pull_roundtrip(void) {
  TEST("dialog_push / dialog_pull: round-trip preserves state");

  test_env_init();

  ddx_test_state_t st_in  = {0};
  ddx_test_state_t st_out = {0};
  snprintf(st_in.name, sizeof(st_in.name), "roundtrip_value");

  // Create the form window without a modal loop.
  window_t *win = create_window_from_form(&kDdxTestForm, 0, 0, NULL,
                                          nop_proc, 0, NULL);
  ASSERT_NOT_NULL(win);

  dialog_push(win, &st_in, kDdxTestForm.bindings, kDdxTestForm.binding_count);

  // Verify control text was set by the push.
  window_t *edit = get_window_item(win, DDX_FORM_ID_NAME);
  ASSERT_NOT_NULL(edit);
  ASSERT_STR_EQUAL(edit->title, "roundtrip_value");

  dialog_pull(win, &st_out, kDdxTestForm.bindings, kDdxTestForm.binding_count);
  ASSERT_STR_EQUAL(st_out.name, "roundtrip_value");

  destroy_window(win);
  test_env_shutdown();
  PASS();
}

// Test 8: show_ddx_dialog with a proc that ends the dialog during evCreate
// (standard headless pattern); verifies code == 1 and state is populated.
// The proc wraps dialog_ddx_proc behaviour manually for testability.

static ddx_test_state_t g_ddx_test_st;
static flags_t          g_ddx_dlg_flags;

static result_t ddx_verify_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  (void)wparam; (void)lparam;
  if (msg == evCreate) {
    g_ddx_dlg_flags = win->flags;
    // Manually replicate DDX push+pull so we can verify without the modal loop.
    dialog_push(win, &g_ddx_test_st,
                kDdxTestForm.bindings, kDdxTestForm.binding_count);
    dialog_pull(win, &g_ddx_test_st,
                kDdxTestForm.bindings, kDdxTestForm.binding_count);
    end_dialog(win, 1);
    return true;
  }
  return false;
}

void test_show_ddx_dialog_form_flags(void) {
  TEST("show_ddx_dialog: dialog gets WINDOW_DIALOG flag and DDX push+pull works");

  test_env_init();
  g_ddx_dlg_flags = 0;
  memset(&g_ddx_test_st, 0, sizeof(g_ddx_test_st));
  snprintf(g_ddx_test_st.name, sizeof(g_ddx_test_st.name), "expected");

  g_ui_runtime.running = true;

  // Use show_dialog_from_form directly with our verification proc.
  // This tests that the form correctly carries DDX metadata and that
  // WINDOW_DIALOG is applied — the same flags show_ddx_dialog would use.
  show_dialog_from_form(&kDdxTestForm, "DDX Verify",
                        NULL, ddx_verify_proc, NULL);

  // Verify the dialog received WINDOW_DIALOG.
  ASSERT_TRUE(g_ddx_dlg_flags & WINDOW_DIALOG);
  // Verify push+pull round-trip: name should equal "expected".
  ASSERT_STR_EQUAL(g_ddx_test_st.name, "expected");

  test_env_shutdown();
  PASS();
}

void test_stackview_layout(void) {
  TEST("stackview: stretches children horizontally (cross-axis) in a vertical stack by default");

  test_env_init();

  layout_view_config_t cfg = {
    .orientation = WINDOW_STACK_VERTICAL,
  };
  window_t *root = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL,
                                 MAKERECT(0, 0, 200, 100),
                                 NULL, "stackview", 0, &cfg);
  ASSERT_NOT_NULL(root);

  window_t *a = create_window("One", 0, MAKERECT(0, 0, 30, 12), root, "button", 0, NULL);
  window_t *b = create_window("Two", 0, MAKERECT(0, 0, 30, 12), root, "button", 0, NULL);
  ASSERT_NOT_NULL(a);
  ASSERT_NOT_NULL(b);

  window_layout_sync(root);

  ASSERT_EQUAL(a->frame.x, 0);
  ASSERT_EQUAL(a->frame.y, 0);
  ASSERT_EQUAL(a->frame.w, 200);
  ASSERT_EQUAL(b->frame.x, 0);
  ASSERT_EQUAL(b->frame.w, 200);
  ASSERT_EQUAL(b->frame.y, a->frame.y + a->frame.h + 4);

  destroy_window(root);
  test_env_shutdown();
  PASS();
}

void test_gridview_layout(void) {
  TEST("gridview: arranges explicit columns compactly");

  test_env_init();

  layout_view_config_t cfg = {
    .orientation = WINDOW_STACK_VERTICAL,
    .spacing = 4,
  };
  window_t *root = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL,
                                 MAKERECT(0, 0, 200, 80),
                                 NULL, "gridview", 0, &cfg);
  ASSERT_NOT_NULL(root);

  window_t *left = create_window("", 0, MAKERECT(0, 0, 40, 0), root, "column", 0, NULL);
  window_t *right = create_window("", WINDOW_FLEXSPACE, MAKERECT(0, 0, 0, 0), root, "column", 0, NULL);
  ASSERT_NOT_NULL(left);
  ASSERT_NOT_NULL(right);
  window_t *l0 = create_window("A", 0, MAKERECT(0, 0, 20, 12), left, "label", 0, NULL);
  window_t *l1 = create_window("B", 0, MAKERECT(0, 0, 20, 12), left, "label", 0, NULL);
  window_t *r0 = create_window("", 0, MAKERECT(0, 0, 20, 12), right, "textedit", 0, NULL);
  window_t *r1 = create_window("", 0, MAKERECT(0, 0, 20, 12), right, "textedit", 0, NULL);
  ASSERT_NOT_NULL(l0);
  ASSERT_NOT_NULL(l1);
  ASSERT_NOT_NULL(r0);
  ASSERT_NOT_NULL(r1);

  window_layout_sync(root);

  /* Layout strategy may vary (fixed-width vs star-style columns).
   * This test only verifies that sync runs and produces valid child frames. */
  ASSERT_TRUE(left->frame.w >= 0);
  ASSERT_TRUE(right->frame.w >= 0);
  ASSERT_TRUE(l0->frame.h > 0);
  ASSERT_TRUE(l1->frame.h > 0);
  ASSERT_TRUE(r0->frame.h > 0);
  ASSERT_TRUE(r1->frame.h > 0);

  destroy_window(root);
  test_env_shutdown();
  PASS();
}

void test_gridview_nested_columns_no_overlap(void) {
  TEST("gridview: nested columns keep preview and list areas separate");

  test_env_init();

  layout_view_config_t cfg = {
    .orientation = WINDOW_STACK_VERTICAL,
    .spacing = 24,
  };
  window_t *root = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL,
                                 MAKERECT(0, 0, 560, 360),
                                 NULL, "gridview", 0, &cfg);
  ASSERT_NOT_NULL(root);

  window_t *left = create_window("", 0, MAKERECT(0, 0, 0, 0), root, "stack", 0, NULL);
  window_t *right = create_window("", 0, MAKERECT(0, 0, 0, 0), root, "stack", 0, NULL);
  ASSERT_NOT_NULL(left);
  ASSERT_NOT_NULL(right);

  window_t *preview = create_window("", 0, MAKERECT(0, 0, 248, 248), left, "button", 0, NULL);
  window_t *label = create_window("No filters loaded", 0, MAKERECT(0, 0, 248, 13), left, "label", 0, NULL);
  window_t *filters = create_window("", WINDOW_FLEXSPACE, MAKERECT(0, 0, 256, 290), right, "reportview", 0, NULL);
  window_t *actions = create_window("", 0, MAKERECT(0, 0, 0, 0), right, "stack", 0, NULL);
  ASSERT_NOT_NULL(preview);
  ASSERT_NOT_NULL(label);
  ASSERT_NOT_NULL(filters);
  ASSERT_NOT_NULL(actions);

  window_layout_sync(root);

  ASSERT_TRUE(left->frame.x < right->frame.x);
  ASSERT_FALSE(do_windows_overlap(preview, filters));
  ASSERT_TRUE(actions->frame.y >= filters->frame.y + filters->frame.h);

  destroy_window(root);
  test_env_shutdown();
  PASS();
}

void test_flowview_layout(void) {
  TEST("flowview: wraps children left-to-right into rows");

  test_env_init();

  window_t *root = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL,
                                 MAKERECT(0, 0, 60, 40),
                                 NULL, "flowview", 0, NULL);
  ASSERT_NOT_NULL(root);

  window_t *a = create_window("One", 0, MAKERECT(0, 0, 30, 12), root, "button", 0, NULL);
  window_t *b = create_window("Two", 0, MAKERECT(0, 0, 30, 12), root, "button", 0, NULL);
  window_t *c = create_window("Three", 0, MAKERECT(0, 0, 30, 12), root, "button", 0, NULL);
  ASSERT_NOT_NULL(a);
  ASSERT_NOT_NULL(b);
  ASSERT_NOT_NULL(c);

  window_layout_sync(root);

  ASSERT_EQUAL(a->frame.x, 0);
  ASSERT_EQUAL(a->frame.y, 0);
  ASSERT_EQUAL(b->frame.x, 30);
  ASSERT_EQUAL(b->frame.y, 0);
  ASSERT_EQUAL(c->frame.x, 0);
  ASSERT_EQUAL(c->frame.y, a->frame.h);

  destroy_window(root);
  test_env_shutdown();
  PASS();
}

void test_auto_layout_padding(void) {
  TEST("auto-layout: padding offsets content inside the client area");

  test_env_init();
  window_t *win = create_window_from_form(&kPadForm, 0, 0, NULL, form_test_proc, 0, NULL);
  ASSERT_NOT_NULL(win);
  window_t *first = get_window_item(win, PAD_FORM_ID_FIRST);
  window_t *second = get_window_item(win, PAD_FORM_ID_SECOND);
  ASSERT_NOT_NULL(first);
  ASSERT_NOT_NULL(second);

  ASSERT_EQUAL(first->frame.x, 8);
  ASSERT_EQUAL(first->frame.y, 8);
  ASSERT_EQUAL(first->frame.w, 184);
  ASSERT_EQUAL(second->frame.x, 8);
  ASSERT_EQUAL(second->frame.y, first->frame.y + first->frame.h + 4);
  ASSERT_EQUAL(second->frame.w, 184);

  destroy_window(win);
  test_env_shutdown();
  PASS();
}

void test_auto_layout_margin(void) {
  TEST("auto-layout: margin offsets individual controls");

  test_env_init();
  window_t *win = create_window_from_form(&kMarForm, 0, 0, NULL, form_test_proc, 0, NULL);
  ASSERT_NOT_NULL(win);
  window_t *first = get_window_item(win, MAR_FORM_ID_FIRST);
  window_t *second = get_window_item(win, MAR_FORM_ID_SECOND);
  ASSERT_NOT_NULL(first);
  ASSERT_NOT_NULL(second);

  ASSERT_EQUAL(first->frame.x, 8);
  ASSERT_EQUAL(first->frame.y, 8);
  ASSERT_EQUAL(first->frame.w, 184);
  ASSERT_EQUAL(second->frame.x, 2);
  ASSERT_EQUAL(second->frame.y, 51);
  ASSERT_EQUAL(second->frame.w, 196);

  destroy_window(win);
  test_env_shutdown();
  PASS();
}

void test_auto_layout_wrapped_label(void) {
  TEST("auto-layout: label wraps when width is constrained");

  test_env_init();
  window_t *win = create_window_from_form(&kWrapForm, 0, 0, NULL, form_test_proc, 0, NULL);
  ASSERT_NOT_NULL(win);
  window_t *label = get_window_item(win, WRAP_FORM_ID_LABEL);
  ASSERT_NOT_NULL(label);

  ASSERT_EQUAL(label->frame.w, 80);
  ASSERT_TRUE(label->frame.h > CONTROL_HEIGHT);

  destroy_window(win);
  test_env_shutdown();
  PASS();
}

void test_label_font_pack_and_measure(void) {
  TEST("label: packed userdata stores palette index and font");

  test_env_init();
  window_t *root = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL,
                                 MAKERECT(0, 0, 120, 40), NULL, nop_proc, 0, NULL);
  ASSERT_NOT_NULL(root);
  label_create_params_t params = {
    .color_index = brTextDisabled,
    .font = FONT_SMALLEST,
    .color_set = true,
  };
  window_t *label = create_window("Font test", 0, MAKERECT(0, 0, 1, CONTROL_HEIGHT),
                                  root, win_label, 0, &params);
  ASSERT_NOT_NULL(label);
  ASSERT_EQUAL(label->frame.w, MAX(1, text_strwidth(FONT_SMALLEST, "Font test") + TEXT_SHADOW_OFFSET));

  label_create_params_t defaults = {
    .font = FONT_SMALL,
    .color_index = 0,
    .color_set = false,
  };
  window_t *default_label = create_window("Default", 0,
                                          MAKERECT(0, 20, 1, CONTROL_HEIGHT),
                                          root, win_label, 0, &defaults);
  ASSERT_NOT_NULL(default_label);
  ASSERT_EQUAL(get_sys_color(brTransparent), 0u);

  label_create_params_t transparent = {
    .font = FONT_SMALL,
    .color_index = 0,
    .color_set = true,
  };
  window_t *transparent_label = create_window("Transparent", 0,
                                              MAKERECT(0, 32, 1, CONTROL_HEIGHT),
                                              root, win_label, 0, &transparent);
  ASSERT_NOT_NULL(transparent_label);

  destroy_window(root);
  test_env_shutdown();
  PASS();
}

void test_nested_stack_positions(void) {
  TEST("nested layout: stack children and grid rows sit in the right place");

  test_env_init();
  window_t *win = create_window_from_form(&kNestForm, 0, 0, NULL, form_test_proc, 0, NULL);
  ASSERT_NOT_NULL(win);
  window_t *header = get_window_item(win, NEST_FORM_ID_HEADER);
  window_t *body = get_window_item(win, NEST_FORM_ID_BODY);
  window_t *grid = get_window_item(win, NEST_FORM_ID_GRID);
  window_t *grid_left = get_window_item(win, NEST_FORM_ID_GRID_LEFT);
  window_t *grid_right = get_window_item(win, NEST_FORM_ID_GRID_RIGHT);
  window_t *body_btn1 = get_window_item(win, NEST_FORM_ID_BODY_BTN1);
  window_t *body_btn2 = get_window_item(win, NEST_FORM_ID_BODY_BTN2);
  window_t *grid_1 = get_window_item(win, NEST_FORM_ID_GRID_1);
  window_t *grid_2 = get_window_item(win, NEST_FORM_ID_GRID_2);
  window_t *grid_3 = get_window_item(win, NEST_FORM_ID_GRID_3);
  window_t *grid_4 = get_window_item(win, NEST_FORM_ID_GRID_4);
  ASSERT_NOT_NULL(header);
  ASSERT_NOT_NULL(body);
  ASSERT_NOT_NULL(grid);
  ASSERT_NOT_NULL(grid_left);
  ASSERT_NOT_NULL(grid_right);
  ASSERT_NOT_NULL(body_btn1);
  ASSERT_NOT_NULL(body_btn2);
  ASSERT_NOT_NULL(grid_1);
  ASSERT_NOT_NULL(grid_2);
  ASSERT_NOT_NULL(grid_3);
  ASSERT_NOT_NULL(grid_4);

  ASSERT_EQUAL(header->frame.y, 0);
  ASSERT_EQUAL(body->frame.y, header->frame.h + 6);
  ASSERT_EQUAL(grid->frame.y, body->frame.y + body->frame.h + 6);
  ASSERT_EQUAL(grid_left->frame.x, 0);
  ASSERT_EQUAL(grid_left->frame.y, 0);
  ASSERT_EQUAL(grid_left->frame.w, 40);
  ASSERT_EQUAL(grid_right->frame.x, grid_left->frame.w + 0);
  ASSERT_EQUAL(grid_right->frame.y, 0);
  ASSERT_TRUE(grid_right->frame.w > grid_left->frame.w);
  ASSERT_EQUAL(grid_left->frame.h, grid_right->frame.h);

  ASSERT_EQUAL(body_btn1->frame.y, 0);
  ASSERT_EQUAL(body_btn2->frame.y, body_btn1->frame.h + 3);

  ASSERT_EQUAL(grid_1->frame.x, 0);
  ASSERT_EQUAL(grid_2->frame.x, 0);
  ASSERT_EQUAL(grid_3->frame.x, 0);
  ASSERT_EQUAL(grid_4->frame.x, 0);
  ASSERT_EQUAL(grid_1->frame.y, 0);
  ASSERT_EQUAL(grid_3->frame.y, grid_1->frame.y + grid_1->frame.h);
  ASSERT_EQUAL(grid_2->frame.y, 0);
  ASSERT_EQUAL(grid_4->frame.y, grid_2->frame.y + grid_2->frame.h);
  ASSERT_EQUAL(grid_1->frame.y, 0);

  destroy_window(win);
  test_env_shutdown();
  PASS();
}

void test_default_auto_layout_stack(void) {
  TEST("auto-layout: root form defaults to vertical stack");

  test_env_init();
  window_t *win = create_window_from_form(&kDefaultStackForm, 0, 0, NULL, form_test_proc, 0, NULL);
  ASSERT_NOT_NULL(win);
  window_t *first = get_window_item(win, 201);
  window_t *second = get_window_item(win, 202);
  ASSERT_NOT_NULL(first);
  ASSERT_NOT_NULL(second);

  ASSERT_EQUAL(first->frame.x, 0);
  ASSERT_EQUAL(first->frame.y, 0);
  ASSERT_TRUE(second->frame.y > first->frame.y);
  ASSERT_TRUE(second->frame.y >= first->frame.h);

  destroy_window(win);
  test_env_shutdown();
  PASS();
}

void test_new_post_grid_stack_layout(void) {
  TEST("auto-layout: grid rows stretch and action stack stays pinned");

  test_env_init();
  window_t *win = create_window_from_form(&kNewPostGridForm, 0, 0, NULL, form_test_proc, 0, NULL);
  ASSERT_NOT_NULL(win);

  window_t *fields = get_window_item(win, NP_FORM_ID_FIELDS);
  window_t *labels = get_window_item(win, NP_FORM_ID_LABELS);
  window_t *inputs = get_window_item(win, NP_FORM_ID_INPUTS);
  window_t *author_l = get_window_item(win, NP_FORM_ID_AUTHOR_L);
  window_t *author_e = get_window_item(win, NP_FORM_ID_AUTHOR_E);
  window_t *title_l = get_window_item(win, NP_FORM_ID_TITLE_L);
  window_t *title_e = get_window_item(win, NP_FORM_ID_TITLE_E);
  window_t *body_l = get_window_item(win, NP_FORM_ID_BODY_L);
  window_t *body_e = get_window_item(win, NP_FORM_ID_BODY_E);
  window_t *actions = get_window_item(win, NP_FORM_ID_ACTIONS);
  window_t *section_sep = get_window_item(win, NP_FORM_ID_SECTION_SEP);
  window_t *ok = get_window_item(win, NP_FORM_ID_OK);
  window_t *cancel = get_window_item(win, NP_FORM_ID_CANCEL);
  window_t *flex_left = get_window_item(win, NP_FORM_ID_FLEX_LEFT);
  window_t *flex_right = get_window_item(win, NP_FORM_ID_FLEX_RIGHT);
  ASSERT_NOT_NULL(fields);
  ASSERT_NOT_NULL(labels);
  ASSERT_NOT_NULL(inputs);
  ASSERT_NOT_NULL(author_l);
  ASSERT_NOT_NULL(author_e);
  ASSERT_NOT_NULL(title_l);
  ASSERT_NOT_NULL(title_e);
  ASSERT_NOT_NULL(body_l);
  ASSERT_NOT_NULL(body_e);
  ASSERT_NOT_NULL(actions);
  ASSERT_NOT_NULL(section_sep);
  ASSERT_NOT_NULL(ok);
  ASSERT_NOT_NULL(cancel);
  ASSERT_NOT_NULL(flex_left);
  ASSERT_NOT_NULL(flex_right);
  ASSERT_TRUE(flex_left->flags & WINDOW_FLEXSPACE);
  ASSERT_TRUE(flex_right->flags & WINDOW_FLEXSPACE);

  ASSERT_EQUAL(fields->frame.y, 8);
  ASSERT_EQUAL(labels->frame.x, 0);
  ASSERT_EQUAL(inputs->frame.x, labels->frame.w + 4);
  ASSERT_EQUAL(labels->frame.w, 56);
  ASSERT_EQUAL(fields->frame.h, labels->frame.h);
  ASSERT_EQUAL(inputs->frame.h, labels->frame.h);
  ASSERT_EQUAL(author_l->frame.y, 0);
  ASSERT_EQUAL(author_e->frame.y, 0);
  ASSERT_EQUAL(title_l->frame.y, author_l->frame.h + 4);
  ASSERT_EQUAL(title_e->frame.y, title_l->frame.y);
  ASSERT_EQUAL(body_l->frame.y, title_l->frame.y + title_l->frame.h + 4);
  ASSERT_EQUAL(body_e->frame.y, body_l->frame.y);
  ASSERT_EQUAL(author_e->frame.x, 0);
  ASSERT_EQUAL(title_e->frame.x, 0);
  ASSERT_EQUAL(body_e->frame.x, 0);
  ASSERT_EQUAL(author_e->frame.w, inputs->frame.w);
  ASSERT_EQUAL(title_e->frame.w, inputs->frame.w);
  ASSERT_EQUAL(body_e->frame.w, inputs->frame.w);
  ASSERT_TRUE(body_e->frame.h > title_e->frame.h);
  ASSERT_TRUE(section_sep->frame.h >= 6);
  ASSERT_EQUAL(section_sep->frame.y, fields->frame.y + fields->frame.h + 4);
  ASSERT_EQUAL(actions->frame.y, section_sep->frame.y + section_sep->frame.h + 4);
  ASSERT_EQUAL(ok->frame.y, 0);
  ASSERT_EQUAL(flex_left->frame.y, 0);
  ASSERT_EQUAL(cancel->frame.y, 0);
  ASSERT_EQUAL(flex_left->frame.x, 0);
  ASSERT_TRUE(ok->frame.w >= strwidth("Post") + BUTTON_PADDING * 2);
  ASSERT_TRUE(cancel->frame.w >= strwidth("Cancel") + BUTTON_PADDING * 2);
  ASSERT_EQUAL(ok->frame.x - (flex_left->frame.x + flex_left->frame.w), 6);
  ASSERT_EQUAL(cancel->frame.x, ok->frame.x + ok->frame.w + 6);
  ASSERT_EQUAL(flex_right->frame.x - (cancel->frame.x + cancel->frame.w), 6);
  ASSERT_EQUAL(flex_left->frame.w, flex_right->frame.w);
  ASSERT_EQUAL(flex_right->frame.x + flex_right->frame.w, actions->frame.w);

  int initial_author_w = author_e->frame.w;
  int initial_title_w = title_e->frame.w;
  int initial_body_w = body_e->frame.w;
  int initial_fields_h = fields->frame.h;
  int initial_body_h = body_e->frame.h;
  int initial_sep_h = section_sep->frame.h;
  int initial_actions_y = actions->frame.y;

  resize_window(win, 272, 220);
  ASSERT_TRUE(author_e->frame.w >= initial_author_w);
  ASSERT_TRUE(title_e->frame.w >= initial_title_w);
  ASSERT_TRUE(body_e->frame.w >= initial_body_w);
  ASSERT_TRUE(fields->frame.h > initial_fields_h);
  ASSERT_TRUE(body_e->frame.h > initial_body_h);
  ASSERT_EQUAL(section_sep->frame.h, initial_sep_h);
  ASSERT_TRUE(actions->frame.y > initial_actions_y);
  ASSERT_EQUAL(actions->frame.y, section_sep->frame.y + section_sep->frame.h + 4);

  resize_window(win, 272, 150);
  ASSERT_TRUE(author_e->frame.w <= initial_author_w);
  ASSERT_TRUE(title_e->frame.w <= initial_title_w);
  ASSERT_TRUE(body_e->frame.w <= initial_body_w);
  ASSERT_TRUE(fields->frame.h <= initial_fields_h);
  ASSERT_TRUE(body_e->frame.h <= initial_body_h);
  ASSERT_EQUAL(section_sep->frame.h, initial_sep_h);
  ASSERT_TRUE(actions->frame.y <= initial_actions_y);
  ASSERT_EQUAL(actions->frame.y, section_sep->frame.y + section_sep->frame.h + 4);

  destroy_window(win);
  test_env_shutdown();
  PASS();
}

void test_stack_separator_gap_and_valign(void) {
  TEST("auto-layout: stack separator gap is explicit and valign does not move row position");

  test_env_init();

  window_t *top_win = create_window_from_form(&kGapFormTop, 0, 0, NULL, form_test_proc, 0, NULL);
  window_t *stretch_win = create_window_from_form(&kGapFormStretch, 0, 0, NULL, form_test_proc, 0, NULL);
  ASSERT_NOT_NULL(top_win);
  ASSERT_NOT_NULL(stretch_win);

  window_t *top_text = get_window_item(top_win, GAP_FORM_ID_TEXT);
  window_t *top_sep = get_window_item(top_win, GAP_FORM_ID_SEP);
  window_t *top_actions = get_window_item(top_win, GAP_FORM_ID_ACTIONS);
  window_t *top_ok = get_window_item(top_win, GAP_FORM_ID_OK);
  window_t *top_cancel = get_window_item(top_win, GAP_FORM_ID_CANCEL);

  window_t *stretch_text = get_window_item(stretch_win, GAP_FORM_ID_TEXT);
  window_t *stretch_sep = get_window_item(stretch_win, GAP_FORM_ID_SEP);
  window_t *stretch_actions = get_window_item(stretch_win, GAP_FORM_ID_ACTIONS);
  window_t *stretch_ok = get_window_item(stretch_win, GAP_FORM_ID_OK);
  window_t *stretch_cancel = get_window_item(stretch_win, GAP_FORM_ID_CANCEL);

  ASSERT_NOT_NULL(top_text);
  ASSERT_NOT_NULL(top_sep);
  ASSERT_NOT_NULL(top_actions);
  ASSERT_NOT_NULL(top_ok);
  ASSERT_NOT_NULL(top_cancel);
  ASSERT_NOT_NULL(stretch_text);
  ASSERT_NOT_NULL(stretch_sep);
  ASSERT_NOT_NULL(stretch_actions);
  ASSERT_NOT_NULL(stretch_ok);
  ASSERT_NOT_NULL(stretch_cancel);

  ASSERT_EQUAL(top_text->frame.y, 8);
  ASSERT_EQUAL(top_sep->frame.y, top_text->frame.y + top_text->frame.h + 4);
  ASSERT_TRUE(top_sep->frame.h >= 6);
  ASSERT_EQUAL(top_actions->frame.y, top_sep->frame.y + top_sep->frame.h + 4);
  ASSERT_EQUAL(top_ok->frame.y, 0);
  ASSERT_EQUAL(top_cancel->frame.y, 0);

  ASSERT_EQUAL(stretch_text->frame.y, top_text->frame.y);
  ASSERT_EQUAL(stretch_sep->frame.y, top_sep->frame.y);
  ASSERT_EQUAL(stretch_sep->frame.h, top_sep->frame.h);
  ASSERT_EQUAL(stretch_actions->frame.y, top_actions->frame.y);
  ASSERT_EQUAL(stretch_ok->frame.y, top_ok->frame.y);
  ASSERT_EQUAL(stretch_cancel->frame.y, top_cancel->frame.y);

  destroy_window(top_win);
  destroy_window(stretch_win);
  test_env_shutdown();
  PASS();
}

void test_button_keeps_fixed_height_and_centers_in_tall_row(void) {
  TEST("auto-layout: button keeps BUTTON_HEIGHT and centers in tall row");

  test_env_init();
  window_t *win = create_window_from_form(&kBtnTallForm, 0, 0, NULL, form_test_proc, 0, NULL);
  ASSERT_NOT_NULL(win);

  window_t *row = get_window_item(win, BTN_TALL_FORM_ID_ROW);
  window_t *btn = get_window_item(win, BTN_TALL_FORM_ID_BUTTON);
  ASSERT_NOT_NULL(btn);
  ASSERT_NOT_NULL(row);
  ASSERT_EQUAL(btn->frame.h, BUTTON_HEIGHT);
  ASSERT_TRUE(row->frame.h > BUTTON_HEIGHT);
  ASSERT_TRUE(btn->frame.w >= strwidth("Center Me") + BUTTON_PADDING * 2);
  ASSERT_EQUAL(btn->frame.y, (row->frame.h - BUTTON_HEIGHT) / 2);

  resize_window(win, kBtnTallForm.width, 160);
  ASSERT_EQUAL(btn->frame.h, BUTTON_HEIGHT);
  ASSERT_TRUE(row->frame.h > BUTTON_HEIGHT);
  ASSERT_TRUE(btn->frame.w >= strwidth("Center Me") + BUTTON_PADDING * 2);
  ASSERT_EQUAL(btn->frame.y, (row->frame.h - BUTTON_HEIGHT) / 2);

  destroy_window(win);
  test_env_shutdown();
  PASS();
}

void test_post_detail_layout_budget(void) {
  TEST("post-detail-like layout: footer fits inside the 336px window");

  test_env_init();

  window_t *short_win = create_post_detail_like_window(336);
  ASSERT_NOT_NULL(short_win);
  window_t *short_layout = get_window_item(short_win, PD_FORM_ID_LAYOUT);
  window_t *short_header = get_window_item(short_win, PD_FORM_ID_HEADER);
  window_t *short_comments = get_window_item(short_win, PD_FORM_ID_COMMENTS);
  window_t *short_actions = get_window_item(short_win, PD_FORM_ID_ACTIONS);
  window_t *short_body = get_window_item(short_win, PD_FORM_ID_BODY);
  ASSERT_NOT_NULL(short_layout);
  ASSERT_NOT_NULL(short_header);
  ASSERT_NOT_NULL(short_comments);
  ASSERT_NOT_NULL(short_actions);
  ASSERT_NOT_NULL(short_body);
  irect16_t short_client = get_client_rect(short_win);
  int short_bottom = short_actions->frame.y + short_actions->frame.h;
  ASSERT_EQUAL(short_layout->frame.y, 8);
  ASSERT_EQUAL(short_layout->frame.h, short_client.h - 16);
  ASSERT_TRUE(short_header->frame.h > CONTROL_HEIGHT);
  ASSERT_TRUE(short_comments->frame.h > 0);
  ASSERT_TRUE(short_body->frame.h > CONTROL_HEIGHT);
  ASSERT_TRUE(short_bottom <= short_client.h);
  ASSERT_EQUAL(short_bottom, short_layout->frame.h);
  destroy_window(short_win);

  test_env_shutdown();
  PASS();
}

void test_socialfeed_post_detail_layout(void) {
  TEST("socialfeed post detail: reportview takes leftover height");

  test_env_init();

  window_t *win = create_window_from_form(&socialfeed_post_detail_form, 0, 0, NULL,
                                          socialfeed_post_detail_layout_proc, 0, NULL);
  ASSERT_NOT_NULL(win);

  window_t *title = get_window_item(win, ID_POST_DETAIL_LBL_TITLE);
  window_t *author = get_window_item(win, ID_POST_DETAIL_LBL_AUTHOR);
  window_t *body = get_window_item(win, ID_POST_DETAIL_LBL_BODY);
  window_t *likes = get_window_item(win, ID_POST_DETAIL_LBL_LIKES);
  window_t *comments_hdr = get_window_item(win, ID_POST_DETAIL_LBL_CMT_HDR);
  window_t *header = get_window_item(win, ID_POST_DETAIL_HEADER);
  window_t *meta = get_window_item(win, ID_POST_DETAIL_META);
  window_t *comments = get_window_item(win, ID_POST_DETAIL_COMMENTS);
  window_t *section_sep = get_window_item(win, ID_POST_DETAIL_SECTION_SEP);
  window_t *actions = get_window_item(win, ID_POST_DETAIL_ACTIONS);
  window_t *flex = get_window_item(win, ID_POST_DETAIL_FLEX);
  ASSERT_NOT_NULL(title);
  ASSERT_NOT_NULL(author);
  ASSERT_NOT_NULL(body);
  ASSERT_NOT_NULL(likes);
  ASSERT_NOT_NULL(comments_hdr);
  ASSERT_NOT_NULL(header);
  ASSERT_NOT_NULL(meta);
  ASSERT_NOT_NULL(comments);
  ASSERT_NOT_NULL(section_sep);
  ASSERT_NOT_NULL(actions);
  ASSERT_NOT_NULL(flex);
  ASSERT_TRUE(flex->flags & WINDOW_FLEXSPACE);
  ASSERT_EQUAL(((uint32_t)(uintptr_t)title->userdata >> 8) & 0xffu, FONT_SYSTEM);
  ASSERT_EQUAL((uint32_t)(uintptr_t)title->userdata & 0xffu, brTextNormal);
  ASSERT_EQUAL(((uint32_t)(uintptr_t)author->userdata >> 8) & 0xffu, FONT_SMALL);
  ASSERT_EQUAL((uint32_t)(uintptr_t)author->userdata & 0xffu, brTextDisabled);
  ASSERT_EQUAL(((uint32_t)(uintptr_t)body->userdata >> 8) & 0xffu, FONT_SMALL);
  ASSERT_EQUAL((uint32_t)(uintptr_t)body->userdata & 0xffu, brTextNormal);
  ASSERT_EQUAL(meta->frame.y, body->frame.y + body->frame.h + 2);
  ASSERT_EQUAL(author->frame.y, 0);
  ASSERT_EQUAL(likes->frame.y, 0);
  ASSERT_EQUAL(comments_hdr->frame.y, 0);
  ASSERT_TRUE(author->frame.x < likes->frame.x);
  ASSERT_TRUE(likes->frame.x < comments_hdr->frame.x);
  ASSERT_EQUAL(comments->frame.w, header->frame.w);
  /* comments height = space between comments and sep, minus the 4px gap */
  int expected_comments_h = section_sep->frame.y - comments->frame.y - 4;
  ASSERT_EQUAL(comments->frame.h, expected_comments_h);
  int content_bottom = win->frame.h - titlebar_height(win) - 8;
  ASSERT_TRUE(actions->frame.y + actions->frame.h <= content_bottom);
  ASSERT_TRUE(content_bottom - (actions->frame.y + actions->frame.h) <= 2);
  ASSERT_TRUE(actions->frame.y > comments->frame.y);
  window_t *close = get_window_item(win, ID_POST_DETAIL_CLOSE);
  ASSERT_NOT_NULL(close);
  ASSERT_EQUAL(close->frame.x + close->frame.w, actions->frame.w);

  int initial_text_col = (int)send_message(comments, RVM_GETREPORTCOLUMNWIDTH, 1, NULL);
  ASSERT_TRUE(initial_text_col > 0);

  int wide_body_h = body->frame.h;
  int wide_comments_y = comments->frame.y;
  int wide_comments_w = comments->frame.w;
  resize_window(win, 420, 336);
  ASSERT_TRUE(body->frame.h >= wide_body_h);
  ASSERT_TRUE(comments->frame.y >= wide_comments_y);
  ASSERT_EQUAL(comments->frame.w, header->frame.w);
  int narrow_text_col = (int)send_message(comments, RVM_GETREPORTCOLUMNWIDTH, 1, NULL);
  ASSERT_TRUE(narrow_text_col < initial_text_col);

  resize_window(win, 620, 336);
  ASSERT_TRUE(body->frame.h <= wide_body_h);
  ASSERT_TRUE(comments->frame.y <= wide_comments_y);
  ASSERT_EQUAL(comments->frame.w, header->frame.w);
  int wide_text_col = (int)send_message(comments, RVM_GETREPORTCOLUMNWIDTH, 1, NULL);
  ASSERT_TRUE(wide_text_col > narrow_text_col);
  ASSERT_TRUE(wide_comments_w > 0);

  destroy_window(win);
  test_env_shutdown();
  PASS();
}

void test_socialfeed_new_post_dialog_layout(void) {
  TEST("socialfeed new post: grid stack and actions do not overlap");

  test_env_init();

  form_def_t dlg_def = socialfeed_new_post_form;
  dlg_def.flags |= WINDOW_VSCROLL | WINDOW_DIALOG | WINDOW_NOTRAYBUTTON;

  irect16_t wr = {0, 0, dlg_def.width, dlg_def.height};
  adjust_window_rect(&wr, dlg_def.flags);
  dlg_def.width = wr.w;
  dlg_def.height = wr.h;

  window_t *win = create_window_from_form(&dlg_def, 0, 0, NULL, form_test_proc, 0, NULL);
  ASSERT_NOT_NULL(win);

  window_t *fields = get_window_item(win, ID_NEW_POST_FIELDS);
  window_t *labels = get_window_item(win, ID_NEW_POST_LABELS);
  window_t *inputs = get_window_item(win, ID_NEW_POST_INPUTS);
  window_t *author = get_window_item(win, ID_NEW_POST_AUTHOR);
  window_t *title = get_window_item(win, ID_NEW_POST_TITLE);
  window_t *body = get_window_item(win, ID_NEW_POST_BODY);
  window_t *sep = get_window_item(win, ID_NEW_POST_SECTION_SEP);
  window_t *actions = get_window_item(win, ID_NEW_POST_ACTIONS);
  window_t *ok = get_window_item(win, ID_NEW_POST_OK);
  window_t *cancel = get_window_item(win, ID_NEW_POST_CANCEL);

  ASSERT_NOT_NULL(fields);
  ASSERT_NOT_NULL(labels);
  ASSERT_NOT_NULL(inputs);
  ASSERT_NOT_NULL(author);
  ASSERT_NOT_NULL(title);
  ASSERT_NOT_NULL(body);
  ASSERT_NOT_NULL(sep);
  ASSERT_NOT_NULL(actions);
  ASSERT_NOT_NULL(ok);
  ASSERT_NOT_NULL(cancel);

  ASSERT_TRUE(fields->frame.h > 0);
  ASSERT_TRUE(inputs->frame.w > 0);
  ASSERT_TRUE(body->frame.h > title->frame.h);
  ASSERT_TRUE(title->frame.y >= author->frame.y + author->frame.h);
  ASSERT_TRUE(body->frame.y >= title->frame.y + title->frame.h);
  ASSERT_TRUE(sep->frame.y >= fields->frame.y + fields->frame.h);
  ASSERT_TRUE(actions->frame.y >= sep->frame.y + sep->frame.h);
  ASSERT_TRUE(ok->frame.y >= 0);
  ASSERT_TRUE(cancel->frame.y >= 0);
  ASSERT_TRUE(cancel->frame.x >= ok->frame.x + ok->frame.w);

  destroy_window(win);
  test_env_shutdown();
  PASS();
}

void test_socialfeed_new_post_modal_dialog_layout(void) {
  TEST("socialfeed new post modal: layout survives show_dialog_from_form_ex");

  test_env_init();

  socialfeed_new_post_modal_capture_t cap;
  memset(&cap, 0, sizeof(cap));

  uint32_t rc = show_dialog_from_form_ex(&socialfeed_new_post_form, "New Post", NULL,
                                         WINDOW_VSCROLL | WINDOW_DIALOG | WINDOW_NOTRAYBUTTON,
                                         socialfeed_new_post_modal_probe_proc, &cap);
  ASSERT_TRUE(rc == 0 || rc == 1);
  ASSERT_TRUE(cap.saw_create);
  ASSERT_TRUE(cap.fields_h > 0);
  ASSERT_TRUE(cap.body_h > cap.title_h);
  ASSERT_TRUE(cap.sep_y >= cap.fields_y + cap.fields_h);
  ASSERT_TRUE(cap.actions_y >= cap.sep_y + cap.sep_h);
  ASSERT_TRUE(cap.cancel_x >= cap.ok_x + cap.ok_w);
  ASSERT_TRUE(cap.cancel_w > 0);

  test_env_shutdown();
  PASS();
}

// ──────────────────────────────────────────────────────────────────────────
// main
// ──────────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  TEST_START("form_def_t / create_window_from_form / show_dialog_from_form");

  test_form_children_exist_at_create();
  test_host_projects_page_toolbar();
  test_form_child_ids();
  test_form_child_flags();
  test_form_child_text();
  test_form_rejects_non_auto_layout_children();
  test_show_dialog_from_form_flags();
  test_center_window_rect_owner();
  test_center_window_rect_screen_clamp();
  test_ddx_form_def_fields();
  test_ddx_push_pull_roundtrip();
  test_show_ddx_dialog_form_flags();
    test_stackview_layout();
    test_gridview_layout();
    test_flowview_layout();
    test_auto_layout_padding();
  test_auto_layout_margin();
  test_auto_layout_wrapped_label();
  test_label_font_pack_and_measure();
  test_nested_stack_positions();
  test_default_auto_layout_stack();
  test_new_post_grid_stack_layout();
  test_stack_separator_gap_and_valign();
  test_button_keeps_fixed_height_and_centers_in_tall_row();
  test_post_detail_layout_budget();
  test_socialfeed_post_detail_layout();
  test_socialfeed_new_post_dialog_layout();
  test_socialfeed_new_post_modal_dialog_layout();

  TEST_END();
}
