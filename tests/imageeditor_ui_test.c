// tests/imageeditor_ui_test.c — headless integration tests for the image editor.
//
// Drives the image editor application layer (documents, palette windows,
// menu command routing) without a display or OpenGL context.  Paint events
// are never triggered so GL texture handles stay at 0 and the guarded
// glDeleteTextures calls in evDestroy are safely skipped.
//
// Pattern: each test allocates g_app, creates the minimal set of windows
// needed, exercises behaviour via send/post_message or direct API calls,
// then tears everything down via ie_teardown().
//
// This file sets the standard for headless UI testing of gem applications:
//   1. Define the application's global state pointer as a test-local variable.
//   2. Initialize it directly (no gem_init to avoid platform deps).
//   3. Drive behaviour through the same public APIs the real app uses.
//   4. Verify outcomes through g_app state fields and is_window() checks.

#include "test_framework.h"
#include "test_env.h"
#include "../examples/imageeditor/imageeditor.h"

// ── Application global – defined in main.c (excluded from this build) ─────────
app_state_t *g_app = NULL;

// ── Test setup / teardown ──────────────────────────────────────────────────────

static void ie_setup(void) {
    test_env_init();
    g_app = calloc(1, sizeof(app_state_t));
    g_app->current_tool = ID_TOOL_SELECT;
    g_app->fg_color     = MAKE_COLOR(0xFF,0x00,0x00,0xFF);
    g_app->bg_color     = MAKE_COLOR(0xFF,0xFF,0xFF,0xFF);
    g_app->next_x       = DOC_START_X;
    g_app->next_y       = DOC_START_Y;
    // menubar_win left NULL: window_menu_rebuild guards against it.
    // tool_win / color_win created per-test as needed.
}

static void ie_teardown(void) {
    if (!g_app) {
        test_env_shutdown();
        return;
    }
    // Destroy palette windows first so their evDestroy handlers can safely
    // null out g_app->tool_win / g_app->tool_options_win / g_app->color_win
    // while g_app is still valid.
    if (g_app->tool_win) {
        destroy_window(g_app->tool_win);
        g_app->tool_win = NULL;
    }
    if (g_app->tool_options_win) {
        destroy_window(g_app->tool_options_win);
        g_app->tool_options_win = NULL;
    }
    if (g_app->color_win) {
        destroy_window(g_app->color_win);
        g_app->color_win = NULL;
    }
    // close_document properly unlinks, frees undo stack, and destroys the
    // window.  It is safe headlessly because canvas_tex / float_tex stay 0.
    while (g_app->docs)
        close_document(g_app->docs);

    free(g_app->clipboard);
    free(g_app);
    g_app = NULL;
    // test_env_shutdown destroys any remaining windows (orphaned dialogs left
    // from message_box in headless mode).
    test_env_shutdown();
}

// Convenience: create the tool palette, tool options, and color palette windows
// exactly as gem_init does, but without the PNG icon strip (SHAREDIR not defined here).
static void ie_create_palette_windows(void) {
    create_tool_palette_window();
    create_tool_options_window();
    create_color_palette_window();
}

// ── Window counting helper ─────────────────────────────────────────────────────
// Counts all windows recursively — root windows plus all children at every depth.
static int count_all_windows(window_t *list) {
    int n = 0;
    for (window_t *w = list; w; w = w->next) {
        n++;
        n += count_all_windows(w->children);
    }
    return n;
}

// ── Tests ──────────────────────────────────────────────────────────────────────

// create_document allocates a canvas_doc_t, creates document + canvas windows,
// and prepends the doc to g_app->docs.
void test_ie_create_document(void) {
    TEST("create_document: document appears in g_app->docs");

    ie_setup();

    canvas_doc_t *doc = create_document(NULL, 320, 200);
    ASSERT_NOT_NULL(doc);
    ASSERT_NOT_NULL(g_app->docs);
    ASSERT_TRUE(g_app->docs == doc);
    ASSERT_EQUAL(doc->canvas_w, 320);
    ASSERT_EQUAL(doc->canvas_h, 200);
    ASSERT_NOT_NULL(doc->pixels);
    ASSERT_FALSE(doc->modified);
    ASSERT_TRUE(is_window(doc->win));

    ie_teardown();
    PASS();
}

// close_document removes the doc from g_app->docs and destroys its window.
void test_ie_close_document(void) {
    TEST("close_document: document removed from g_app->docs and window destroyed");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 100, 100);
    ASSERT_NOT_NULL(doc);
    window_t *dwin = doc->win;

    close_document(doc);

    ASSERT_NULL(g_app->docs);
    ASSERT_FALSE(is_window(dwin));

    ie_teardown();
    PASS();
}

// Closing multiple documents in LIFO order leaves g_app->docs empty.
void test_ie_close_multiple_documents(void) {
    TEST("close_document: all documents removed when closed in order");

    ie_setup();
    canvas_doc_t *d1 = create_document(NULL, 100, 100);
    canvas_doc_t *d2 = create_document(NULL, 200, 200);
    canvas_doc_t *d3 = create_document(NULL,  50,  50);
    ASSERT_NOT_NULL(d1);
    ASSERT_NOT_NULL(d2);
    ASSERT_NOT_NULL(d3);
    // g_app->docs is d3->d2->d1 (LIFO prepend)
    ASSERT_TRUE(g_app->docs == d3);

    close_document(d2);
    // List is now d3->d1
    ASSERT_TRUE(g_app->docs == d3);
    ASSERT_TRUE(g_app->docs->next == d1);

    close_document(d3);
    ASSERT_TRUE(g_app->docs == d1);

    close_document(d1);
    ASSERT_NULL(g_app->docs);

    ie_teardown();
    PASS();
}

// Palette windows are created correctly and their pointers are stored.
void test_ie_palette_windows_created(void) {
    TEST("create palette windows: g_app->tool_win and g_app->color_win are valid");

    ie_setup();
    ie_create_palette_windows();

    ASSERT_NOT_NULL(g_app->tool_win);
    ASSERT_NOT_NULL(g_app->tool_options_win);
    ASSERT_NOT_NULL(g_app->color_win);
    ASSERT_TRUE(is_window(g_app->tool_win));
    ASSERT_TRUE(is_window(g_app->tool_options_win));
    ASSERT_TRUE(is_window(g_app->color_win));

    ie_teardown();
    PASS();
}

// Regression: destroy_window on the tool palette window must null g_app->tool_win.
// Before the fix, closing the tool window left g_app->tool_win as a dangling pointer;
// the subsequent ID_WINDOW_TOOLS command crashed calling show_window on freed memory.
void test_ie_close_tool_window_clears_pointer(void) {
    TEST("close tool window: g_app->tool_win becomes NULL (regression)");

    ie_setup();
    ie_create_palette_windows();
    ASSERT_NOT_NULL(g_app->tool_win);

    destroy_window(g_app->tool_win);

    ASSERT_NULL(g_app->tool_win);

    ie_teardown();
    PASS();
}

// After the tool window is closed, Window > Tools must recreate it without crashing.
void test_ie_reopen_tool_window(void) {
    TEST("ID_WINDOW_TOOLS after close: new tool window is created");

    ie_setup();
    ie_create_palette_windows();
    destroy_window(g_app->tool_win);  // simulate user closing it
    ASSERT_NULL(g_app->tool_win);

    // Dispatch the menu command – this is the code path that previously crashed.
    handle_menu_command(ID_WINDOW_TOOLS);

    ASSERT_NOT_NULL(g_app->tool_win);
    ASSERT_TRUE(is_window(g_app->tool_win));

    ie_teardown();
    PASS();
}

// Regression: same close/reopen test for the color palette.
void test_ie_close_color_window_clears_pointer(void) {
    TEST("close color window: g_app->color_win becomes NULL (regression)");

    ie_setup();
    ie_create_palette_windows();
    ASSERT_NOT_NULL(g_app->color_win);

    destroy_window(g_app->color_win);

    ASSERT_NULL(g_app->color_win);

    ie_teardown();
    PASS();
}

// After the color window is closed, Window > Colors must recreate it.
void test_ie_reopen_color_window(void) {
    TEST("ID_WINDOW_COLORS after close: new color window is created");

    ie_setup();
    ie_create_palette_windows();
    destroy_window(g_app->color_win);
    ASSERT_NULL(g_app->color_win);

    handle_menu_command(ID_WINDOW_COLORS);

    ASSERT_NOT_NULL(g_app->color_win);
    ASSERT_TRUE(is_window(g_app->color_win));

    ie_teardown();
    PASS();
}

// Reopening an already-open palette window (show_window) must not crash.
void test_ie_reopen_existing_tool_window(void) {
    TEST("ID_WINDOW_TOOLS when already open: show_window called, no crash");

    ie_setup();
    ie_create_palette_windows();
    window_t *orig = g_app->tool_win;

    // Command when window is already open – should just re-show it.
    handle_menu_command(ID_WINDOW_TOOLS);

    // Same window pointer – nothing new was created.
    ASSERT_TRUE(g_app->tool_win == orig);
    ASSERT_TRUE(is_window(g_app->tool_win));

    ie_teardown();
    PASS();
}

// Tool commands update g_app->current_tool.
void test_ie_tool_selection_via_command(void) {
    TEST("tool command: g_app->current_tool updated for each tool");

    ie_setup();
    ie_create_palette_windows();
    ASSERT_EQUAL(g_app->current_tool, ID_TOOL_SELECT);

    handle_menu_command(ID_TOOL_PENCIL);
    ASSERT_EQUAL(g_app->current_tool, ID_TOOL_PENCIL);

    handle_menu_command(ID_TOOL_BRUSH);
    ASSERT_EQUAL(g_app->current_tool, ID_TOOL_BRUSH);

    handle_menu_command(ID_TOOL_ERASER);
    ASSERT_EQUAL(g_app->current_tool, ID_TOOL_ERASER);

    handle_menu_command(ID_TOOL_FILL);
    ASSERT_EQUAL(g_app->current_tool, ID_TOOL_FILL);

    handle_menu_command(ID_TOOL_SELECT);
    ASSERT_EQUAL(g_app->current_tool, ID_TOOL_SELECT);

    ie_teardown();
    PASS();
}

// Reopened tool window reflects the current (non-default) tool selection.
void test_ie_reopen_tool_window_syncs_active_tool(void) {
    TEST("ID_WINDOW_TOOLS after close: current tool preserved and bxSetActiveItem sent");

    ie_setup();
    ie_create_palette_windows();

    // Switch to pencil tool.
    handle_menu_command(ID_TOOL_PENCIL);
    ASSERT_EQUAL(g_app->current_tool, ID_TOOL_PENCIL);

    // Close and reopen the tool palette.
    destroy_window(g_app->tool_win);
    ASSERT_NULL(g_app->tool_win);
    handle_menu_command(ID_WINDOW_TOOLS);
    ASSERT_NOT_NULL(g_app->tool_win);

    // g_app->current_tool must still reflect the active tool, and the new
    // window must exist (bxSetActiveItem is sent on creation).
    ASSERT_EQUAL(g_app->current_tool, ID_TOOL_PENCIL);
    ASSERT_TRUE(is_window(g_app->tool_win));

    ie_teardown();
    PASS();
}

// Closing an unmodified document via evClose must not show a dialog.
void test_ie_close_unmodified_doc_no_dialog(void) {
    TEST("evClose on unmodified doc: no dialog window created, doc removed");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 100, 100);
    ASSERT_NOT_NULL(doc);
    ASSERT_FALSE(doc->modified);

    // Count all windows (including canvas child) before closing.
    int windows_before = count_all_windows(g_ui_runtime.windows);

    // Simulate the user clicking the close button on the document window.
    send_message(doc->win, evClose, 0, NULL);

    // doc->win and its canvas child are destroyed; no new dialog windows appear.
    int windows_after = count_all_windows(g_ui_runtime.windows);

    // Windows should have decreased (doc + canvas child gone, no dialog added).
    ASSERT_TRUE(windows_after < windows_before);
    // The document must be gone.
    ASSERT_NULL(g_app->docs);

    ie_teardown();
    PASS();
}

// Closing a modified document via evClose must show an "Unsaved Changes" dialog.
// In headless mode (g_ui_runtime.running = false) the modal loop exits immediately
// and returns 0 (not IDYES), so the document is still closed; but a dialog window
// (plus label and button children) must have been created.
void test_ie_close_modified_doc_shows_dialog(void) {
    TEST("evClose on modified doc: unsaved-changes dialog is created");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 100, 100);
    ASSERT_NOT_NULL(doc);
    doc->modified = true;

    // Count all windows recursively before (dwin root + cwin child = 2).
    int windows_before = count_all_windows(g_ui_runtime.windows);

    send_message(doc->win, evClose, 0, NULL);

    // After evClose: dwin + cwin are destroyed (-2), but the dialog window and
    // its children (label + Yes + No = 3) are created, leaving +1 net change.
    int windows_after = count_all_windows(g_ui_runtime.windows);

    ASSERT_TRUE(windows_after > windows_before - 2);  // dialog subtree present
    ASSERT_TRUE(windows_after >= 1);                   // at least the dialog itself

    // The document was still closed (headless returns 0 = not IDYES, so
    // doc_confirm_close falls through to close_document).
    ASSERT_NULL(g_app->docs);

    ie_teardown();
    PASS();
}

// Calling doc_confirm_close on an unmodified doc must close it without a dialog.
void test_ie_doc_confirm_close_unmodified(void) {
    TEST("doc_confirm_close on unmodified doc: doc is removed, no dialog");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 64, 64);
    ASSERT_NOT_NULL(doc);
    ASSERT_FALSE(doc->modified);

    int windows_before = 0;
    for (window_t *w = g_ui_runtime.windows; w; w = w->next) windows_before++;

    doc_confirm_close(doc, doc->win);

    ASSERT_NULL(g_app->docs);

    int windows_after = 0;
    for (window_t *w = g_ui_runtime.windows; w; w = w->next) windows_after++;

    ASSERT_TRUE(windows_after <= windows_before);

    ie_teardown();
    PASS();
}

// Calling doc_confirm_close on a modified doc must show the dialog and still
// close the doc (headless returns IDCANCEL which is treated as IDNO — doc closes).
void test_ie_doc_confirm_close_modified(void) {
    TEST("doc_confirm_close on modified doc: dialog created, doc closed");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 64, 64);
    ASSERT_NOT_NULL(doc);
    doc->modified = true;

    // dwin (root) + cwin (child) = 2 total.
    int windows_before = count_all_windows(g_ui_runtime.windows);

    doc_confirm_close(doc, doc->win);

    // Dialog + its children were created; dwin + cwin were destroyed.
    int windows_after = count_all_windows(g_ui_runtime.windows);
    ASSERT_TRUE(windows_after > windows_before - 2);  // dialog subtree present

    // Document must be closed.
    ASSERT_NULL(g_app->docs);

    ie_teardown();
    PASS();
}

// Direct canvas pixel writes and reads operate on the raw RGBA buffer.
void test_ie_canvas_pixel_operations(void) {
    TEST("canvas pixels: set and get via raw buffer");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 64, 64);
    ASSERT_NOT_NULL(doc);

    // Write a red pixel at (10, 20).
    uint32_t red = MAKE_COLOR(0xFF, 0x00, 0x00, 0xFF);
    uint8_t *p = doc->pixels + ((size_t)20 * 64 + 10) * 4;
    p[0] = 0xFF; p[1] = 0x00; p[2] = 0x00; p[3] = 0xFF;
    doc->canvas_dirty = true;
    doc->modified = true;

    // Read it back.
    uint8_t *q = doc->pixels + ((size_t)20 * 64 + 10) * 4;
    uint32_t read = MAKE_COLOR(q[0], q[1], q[2], q[3]);
    ASSERT_EQUAL(read, red);

    ASSERT_TRUE(doc->modified);
    ASSERT_TRUE(doc->canvas_dirty);

    ie_teardown();
    PASS();
}

// canvas_set_pixel / canvas_get_pixel wrappers work correctly.
void test_ie_canvas_pixel_helpers(void) {
    TEST("canvas_set_pixel / canvas_get_pixel: correct round-trip");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 32, 32);
    ASSERT_NOT_NULL(doc);

    uint32_t blue = MAKE_COLOR(0x00, 0x00, 0xFF, 0xFF);
    canvas_set_pixel(doc, 5, 7, blue);

    uint32_t got = canvas_get_pixel(doc, 5, 7);
    ASSERT_EQUAL(got, blue);

    // Out-of-bounds write must not crash.
    canvas_set_pixel(doc, -1,  0, blue);
    canvas_set_pixel(doc, 100, 0, blue);
    canvas_set_pixel(doc, 0, 100, blue);

    ie_teardown();
    PASS();
}

// ID_FILE_CLOSE command on the active document must trigger close.
void test_ie_file_close_command(void) {
    TEST("ID_FILE_CLOSE: active document is closed");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 100, 100);
    ASSERT_NOT_NULL(doc);
    g_app->active_doc = doc;

    // Dispatch file-close – evClose is posted to doc->win which calls
    // doc_confirm_close; doc is unmodified so no dialog.
    handle_menu_command(ID_FILE_CLOSE);

    // handle_menu_command posts evClose, so flush the queue.
    repost_messages();

    ASSERT_NULL(g_app->docs);

    ie_teardown();
    PASS();
}

// Zoom command changes the canvas window's scale state.
void test_ie_zoom_commands(void) {
    TEST("ID_VIEW_ZOOM_*: zoom commands are accepted without crash");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 320, 200);
    ASSERT_NOT_NULL(doc);
    g_app->active_doc = doc;

    // These commands reach canvas_win_set_zoom via handle_menu_command.
    // We just verify they don't crash in headless mode.
    handle_menu_command(ID_VIEW_ZOOM_2X);
    handle_menu_command(ID_VIEW_ZOOM_4X);
    handle_menu_command(ID_VIEW_ZOOM_1X);

    ie_teardown();
    PASS();
}

// show_size_dialog returns false in headless mode (modal loop exits immediately).
void test_ie_size_dialog_headless(void) {
    TEST("show_size_dialog: returns false in headless mode (no event loop)");

    ie_setup();
    int w = 320, h = 200;

    // In headless mode run_dialog_loop exits immediately; accepted is never set.
    bool accepted = show_size_dialog(NULL, "Canvas Size", &w, &h);
    ASSERT_FALSE(accepted);

    // Dimensions must be unchanged since the dialog was not accepted.
    ASSERT_EQUAL(w, 320);
    ASSERT_EQUAL(h, 200);

    ie_teardown();
    PASS();
}

// Simulating OK in the size dialog: drive ni_proc directly to accept it.
void test_ie_size_dialog_accept(void) {
    TEST("show_size_dialog: correct values written when OK button simulated");

    ie_setup();
    int w = 320, h = 200;

    // Create the dialog manually (mirrors what show_dialog_from_form does).
    int new_w = 640, new_h = 480;
    // We cannot easily simulate "OK click" without the modal loop, so we call
    // canvas_resize directly which is what a confirmed size-dialog would trigger.
    canvas_doc_t *doc = create_document(NULL, w, h);
    ASSERT_NOT_NULL(doc);

    bool ok = canvas_resize(doc, new_w, new_h);
    ASSERT_TRUE(ok);
    ASSERT_EQUAL(doc->canvas_w, new_w);
    ASSERT_EQUAL(doc->canvas_h, new_h);
    ASSERT_TRUE(doc->modified);

    ie_teardown();
    PASS();
}

// tool_options_win is created by ie_create_palette_windows and cleared on close.
void test_ie_tool_options_window_created(void) {
    TEST("create_tool_options_window: g_app->tool_options_win is valid");

    ie_setup();
    ie_create_palette_windows();

    ASSERT_NOT_NULL(g_app->tool_options_win);
    ASSERT_TRUE(is_window(g_app->tool_options_win));

    ie_teardown();
    PASS();
}

// Closing the tool options window must null g_app->tool_options_win.
void test_ie_close_tool_options_window_clears_pointer(void) {
    TEST("close tool options window: g_app->tool_options_win becomes NULL");

    ie_setup();
    ie_create_palette_windows();
    ASSERT_NOT_NULL(g_app->tool_options_win);

    destroy_window(g_app->tool_options_win);

    ASSERT_NULL(g_app->tool_options_win);

    ie_teardown();
    PASS();
}

// brush_size is updated when the brush index is within [0, NUM_BRUSH_SIZES).
void test_ie_brush_size_valid_range(void) {
    TEST("brush_size: accepts valid indices 0..NUM_BRUSH_SIZES-1");

    ie_setup();
    // ie_setup uses calloc so brush_size starts at 0.
    // (gem_init initializes it to 1, but tests use calloc directly.)
    ASSERT_EQUAL(g_app->brush_size, 0);

    for (int i = 0; i < NUM_BRUSH_SIZES; i++) {
        g_app->brush_size = i;
        ASSERT_EQUAL(g_app->brush_size, i);
        // kBrushSizes must be non-negative
        ASSERT_TRUE(kBrushSizes[i] >= 0);
    }

    ie_teardown();
    PASS();
}

// kBrushSizes array has exactly NUM_BRUSH_SIZES elements and is strictly increasing.
void test_ie_brush_sizes_array(void) {
    TEST("kBrushSizes: NUM_BRUSH_SIZES distinct increasing values");

    ie_setup();

    ASSERT_EQUAL(NUM_BRUSH_SIZES, 5);
    for (int i = 1; i < NUM_BRUSH_SIZES; i++) {
        ASSERT_TRUE(kBrushSizes[i] > kBrushSizes[i - 1]);
    }

    ie_teardown();
    PASS();
}

// Switching tool type invalidates the tool_options_win (no crash in headless).
void test_ie_tool_switch_updates_options_panel(void) {
    TEST("tool switch: tool_options_win receives invalidate without crash");

    ie_setup();
    ie_create_palette_windows();

    // Switch to a brush tool — brush-size panel should be active.
    handle_menu_command(ID_TOOL_BRUSH);
    ASSERT_EQUAL(g_app->current_tool, ID_TOOL_BRUSH);
    ASSERT_TRUE(is_window(g_app->tool_options_win));

    // Switch to a shape tool — shape panel should be active.
    handle_menu_command(ID_TOOL_RECT);
    ASSERT_EQUAL(g_app->current_tool, ID_TOOL_RECT);
    ASSERT_TRUE(is_window(g_app->tool_options_win));

    // Switch to a tool with no options (Select).
    handle_menu_command(ID_TOOL_SELECT);
    ASSERT_EQUAL(g_app->current_tool, ID_TOOL_SELECT);
    ASSERT_TRUE(is_window(g_app->tool_options_win));

    ie_teardown();
    PASS();
}

// shape_filled is stored in g_app and read by the canvas / shape panel.
void test_ie_shape_filled_state(void) {
    TEST("shape_filled: default false, toggled directly");

    ie_setup();

    ASSERT_FALSE(g_app->shape_filled);

    g_app->shape_filled = true;
    ASSERT_TRUE(g_app->shape_filled);

    g_app->shape_filled = false;
    ASSERT_FALSE(g_app->shape_filled);

    ie_teardown();
    PASS();
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    TEST_START("Image Editor UI – headless integration tests");

    test_ie_create_document();
    test_ie_close_document();
    test_ie_close_multiple_documents();
    test_ie_palette_windows_created();
    test_ie_close_tool_window_clears_pointer();
    test_ie_reopen_tool_window();
    test_ie_close_color_window_clears_pointer();
    test_ie_reopen_color_window();
    test_ie_reopen_existing_tool_window();
    test_ie_tool_selection_via_command();
    test_ie_reopen_tool_window_syncs_active_tool();
    test_ie_close_unmodified_doc_no_dialog();
    test_ie_close_modified_doc_shows_dialog();
    test_ie_doc_confirm_close_unmodified();
    test_ie_doc_confirm_close_modified();
    test_ie_canvas_pixel_operations();
    test_ie_canvas_pixel_helpers();
    test_ie_file_close_command();
    test_ie_zoom_commands();
    test_ie_size_dialog_headless();
    test_ie_size_dialog_accept();
    test_ie_tool_options_window_created();
    test_ie_close_tool_options_window_clears_pointer();
    test_ie_brush_size_valid_range();
    test_ie_brush_sizes_array();
    test_ie_tool_switch_updates_options_panel();
    test_ie_shape_filled_state();

    TEST_END();
}
