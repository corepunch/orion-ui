// Tests for create_window_from_form() and show_dialog_from_form().
//
// Verifies:
//  1. Children declared in a form_def_t exist and are findable via
//     get_window_item() when kWindowMessageCreate fires on the parent.
//  2. Child IDs, flags, and initial text are applied correctly.
//  3. show_dialog_from_form() creates a window with WINDOW_DIALOG set,
//     applies the title override, and includes WINDOW_VSCROLL.

#include "test_framework.h"
#include "test_env.h"
#include "../ui.h"

// ──────────────────────────────────────────────────────────────────────────
// Shared form definition used by several tests
// ──────────────────────────────────────────────────────────────────────────

#define FORM_ID_NAME   1
#define FORM_ID_OK     2
#define FORM_ID_CANCEL 3

static const form_ctrl_def_t kTestFormChildren[] = {
  { FORM_CTRL_TEXTEDIT, FORM_ID_NAME,   {60,  8, 80, 13}, 0,              "hello", "name"   },
  { FORM_CTRL_BUTTON,   FORM_ID_OK,     {50, 30, 40, 13}, BUTTON_DEFAULT, "OK",    "ok"     },
  { FORM_CTRL_BUTTON,   FORM_ID_CANCEL, {94, 30, 50, 13}, 0,              "Cancel","cancel" },
};

static const form_def_t kTestForm = {
  .name        = "Test Form",
  .width       = 160,
  .height      = 52,
  .flags       = 0,
  .children    = kTestFormChildren,
  .child_count = 3,
};

// ──────────────────────────────────────────────────────────────────────────
// State captured in the window proc during kWindowMessageCreate
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

static result_t form_test_proc(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam) {
  (void)wparam; (void)lparam;
  if (msg == kWindowMessageCreate) {
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
// Test 1: children exist at kWindowMessageCreate
// ──────────────────────────────────────────────────────────────────────────

void test_form_children_exist_at_create(void) {
  TEST("create_window_from_form: children findable in kWindowMessageCreate");

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

// ──────────────────────────────────────────────────────────────────────────
// Test 5: show_dialog_from_form applies WINDOW_DIALOG flag and title override
// ──────────────────────────────────────────────────────────────────────────

// Window proc used for the dialog-flag test: immediately ends the dialog.
static flags_t   g_dlg_flags     = 0;
static char      g_dlg_title[64] = {0};

static result_t dialog_flag_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  (void)wparam; (void)lparam;
  if (msg == kWindowMessageCreate) {
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

// ──────────────────────────────────────────────────────────────────────────
// main
// ──────────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  TEST_START("form_def_t / create_window_from_form / show_dialog_from_form");

  test_form_children_exist_at_create();
  test_form_child_ids();
  test_form_child_flags();
  test_form_child_text();
  test_show_dialog_from_form_flags();

  TEST_END();
}
