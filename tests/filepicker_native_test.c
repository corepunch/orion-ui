#include "test_framework.h"
#include <assert.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_OSX
#define axFileDialogsRequireNative test_requires_native
#define axGetOpenFileName test_native_open
#define axGetSaveFileName test_native_save
#define axGetFolderName test_native_folder
#define show_dialog_from_form_ex test_custom_dialog
#define ui_tr test_translate
#include "../orion/commdlg/filepicker.c"
#undef axFileDialogsRequireNative
#undef axGetOpenFileName
#undef axGetSaveFileName
#undef axGetFolderName
#undef show_dialog_from_form_ex
#undef ui_tr

static bool requires_native = true, accept_native;
static int native_kind, custom_count;
static const char *expected_title;

bool_t test_requires_native(void) { return requires_native; }
const char *test_translate(const char *text) { return !strcmp(text, "Songs") ? "Chansons" : text; }
static bool_t test_native_result(AXopenfilename const *ofn, int kind) {
  native_kind = kind;
  assert(!strcmp(ofn->lpstrTitle, expected_title));
  assert(!strcmp(ofn->lpstrFilter, "Chansons"));
  assert(!strcmp(ofn->lpstrFilter + 9, "*.groove;*.wav"));
  assert(ofn->nFilterIndex == 2);
  assert(ofn->Flags == (AX_OFN_FILEMUSTEXIST | AX_OFN_PATHMUSTEXIST));
  if (accept_native) strcpy(ofn->lpstrFile, "/tmp/Selected.groove");
  return accept_native;
}
bool_t test_native_open(AXopenfilename const *ofn)   { return test_native_result(ofn, 1); }
bool_t test_native_save(AXopenfilename const *ofn)   { return test_native_result(ofn, 2); }
bool_t test_native_folder(AXopenfilename const *ofn) { return test_native_result(ofn, 3); }
uint32_t test_custom_dialog(form_def_t const *def, char const *title, window_t *parent,
    flags_t flags, winproc_t proc, void *param) {
  (void)def; (void)title; (void)parent; (void)flags; (void)proc; (void)param;
  custom_count++;
  return 0;
}

static void test_native_routing(void) {
  TEST("Native file picker routing preserves requests and cancellation");
  char path[128] = "Original.groove";
  openfilename_t ofn = { .lpstrFile = path, .nMaxFile = sizeof(path),
    .lpstrFilter = "Songs\0*.groove;*.wav\0All Files\0*.*\0", .nFilterIndex = 2,
    .Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST };
  expected_title = "Open File";
  ASSERT_FALSE(get_open_filename(&ofn));
  ASSERT_EQUAL(native_kind, 1);
  expected_title = "Save File";
  ASSERT_FALSE(get_save_filename(&ofn));
  ASSERT_EQUAL(native_kind, 2);
  expected_title = "Select Folder";
  ASSERT_FALSE(get_folder_name(&ofn));
  ASSERT_EQUAL(native_kind, 3);
  ASSERT_EQUAL(custom_count, 0);
  ASSERT_STR_EQUAL(path, "Original.groove");
  ofn.Flags &= ~OFN_PICKFOLDER;
  accept_native = true; expected_title = "Open File";
  ASSERT_TRUE(get_open_filename(&ofn));
  ASSERT_STR_EQUAL(path, "/tmp/Selected.groove");
  expected_title = "Save File";
  ASSERT_TRUE(get_save_filename(&ofn));
  accept_native = false;
  PASS();
}

static void test_custom_routing(void) {
  TEST("Unsandboxed apps retain the custom picker");
  requires_native = false; native_kind = 0;
  char path[128] = "Original.groove";
  openfilename_t ofn = { .lpstrFile = path, .nMaxFile = sizeof(path) };
  ASSERT_FALSE(get_open_filename(&ofn));
  ASSERT_FALSE(get_save_filename(&ofn));
  ASSERT_FALSE(get_folder_name(&ofn));
  ASSERT_EQUAL(custom_count, 3);
  ASSERT_EQUAL(native_kind, 0);
  PASS();
}

static void test_invalid_request(void) {
  TEST("Invalid picker requests never reach a native panel");
  requires_native = true; native_kind = 0;
  ASSERT_FALSE(get_open_filename(NULL));
  char path[4] = { 'a', 'b', 'c', 'd' };
  openfilename_t ofn = { .lpstrFile = path, .nMaxFile = sizeof(path) };
  ASSERT_FALSE(get_save_filename(&ofn));
  ASSERT_EQUAL(native_kind, 0);
  PASS();
}
#endif

int main(void) {
  TEST_START("Native file picker");
#if defined(__APPLE__) && TARGET_OS_OSX
  test_native_routing(); test_custom_routing(); test_invalid_request();
#endif
  TEST_END();
}
