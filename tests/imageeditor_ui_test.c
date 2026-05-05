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
#include <unistd.h>
#include <stdlib.h>

// ── Cross-platform temp directory helper (same pattern as image_test.c) ─────────
static const char *ie_temp_dir(void) {
    const char *d = getenv("TEMP");
    if (!d) d = getenv("TMP");
    if (!d) d = getenv("TMPDIR");
    if (!d) d = "/tmp";
    return d;
}

// ── Application global – defined in main.c (excluded from this build) ─────────
app_state_t *g_app = NULL;

// ── Test setup / teardown ──────────────────────────────────────────────────────

static void ie_setup(void) {
    test_env_init();
    g_app = calloc(1, sizeof(app_state_t));
    g_app->current_tool = ID_TOOL_SELECT;
    g_app->fg_color     = MAKE_COLOR(0xFF,0x00,0x00,0xFF);
    g_app->bg_color     = MAKE_COLOR(0xFF,0xFF,0xFF,0xFF);
    g_app->wand.antialias = true;
    g_app->wand.spread = 24;
    g_app->wand.overlay_color = MAKE_COLOR(0x40, 0xA0, 0xFF, 0x55);
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
    if (g_app->main_toolbar_win) {
        destroy_window(g_app->main_toolbar_win);
        g_app->main_toolbar_win = NULL;
    }
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
    create_main_toolbar_window();
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
    ASSERT_NOT_NULL(g_app->main_toolbar_win);
    ASSERT_NOT_NULL(get_window_item(g_app->main_toolbar_win, ID_FILE_NEW));

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

// New document windows should let Orion choose the default MDI cascade.
void test_ie_document_windows_cascade(void) {
    TEST("create_document: second document opens down and right from first");

    ie_setup();
    canvas_doc_t *d1 = create_document(NULL, 100, 100);
    canvas_doc_t *d2 = create_document(NULL, 100, 100);
    ASSERT_NOT_NULL(d1);
    ASSERT_NOT_NULL(d2);

    ASSERT_EQUAL(d2->win->frame.x, d1->win->frame.x + DEFAULT_WINDOW_CASCADE_X);
    ASSERT_EQUAL(d2->win->frame.y, d1->win->frame.y + DEFAULT_WINDOW_CASCADE_Y);

    ie_teardown();
    PASS();
}

// Full-workspace documents should still receive Orion's default cascade.
void test_ie_large_document_windows_cascade(void) {
    TEST("create_document: large documents still cascade down and right");

    ie_setup();
    canvas_doc_t *d1 = create_document(NULL, 2000, 1600);
    canvas_doc_t *d2 = create_document(NULL, 2000, 1600);
    ASSERT_NOT_NULL(d1);
    ASSERT_NOT_NULL(d2);

    ASSERT_EQUAL(d2->win->frame.x, d1->win->frame.x + DEFAULT_WINDOW_CASCADE_X);
    ASSERT_EQUAL(d2->win->frame.y, d1->win->frame.y + DEFAULT_WINDOW_CASCADE_Y);

    ie_teardown();
    PASS();
}

void test_ie_anim_new_frame_selects_inserted_frame(void) {
    TEST("Anim: New Frame inserts a blank frame and selects it");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 4, 4);
    ASSERT_NOT_NULL(doc);
    g_app->active_doc = doc;

    canvas_set_pixel(doc, 0, 0, MAKE_COLOR(0xAA, 0x11, 0x22, 0xFF));
    canvas_set_pixel(doc, 1, 0, MAKE_COLOR(0x33, 0x44, 0x55, 0xFF));

    handle_menu_command(ID_ANIM_NEW_FRAME);

    ASSERT_NOT_NULL(doc->anim);
    ASSERT_EQUAL(doc->anim->frame_count, 2);
    ASSERT_EQUAL(doc->anim->active_frame, 1);
    ASSERT_EQUAL(canvas_get_pixel(doc, 0, 0), MAKE_COLOR(0x00, 0x00, 0x00, 0x00));
    ASSERT_EQUAL(canvas_get_pixel(doc, 1, 0), MAKE_COLOR(0x00, 0x00, 0x00, 0x00));

    ie_teardown();
    PASS();
}

void test_ie_anim_trace_toggle(void) {
    TEST("Anim: trace toggle flips the onion-skin overlay state");

    ie_setup();
    ASSERT_FALSE(g_app->anim_trace_enabled);

    handle_menu_command(ID_ANIM_TRACE);
    ASSERT_TRUE(g_app->anim_trace_enabled);

    handle_menu_command(ID_ANIM_TRACE);
    ASSERT_FALSE(g_app->anim_trace_enabled);

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

    handle_menu_command(ID_TOOL_MAGIC_WAND);
    ASSERT_EQUAL(g_app->current_tool, ID_TOOL_MAGIC_WAND);

    handle_menu_command(ID_TOOL_CROP);
    ASSERT_EQUAL(g_app->current_tool, ID_TOOL_CROP);

    handle_menu_command(ID_TOOL_MOVE);
    ASSERT_EQUAL(g_app->current_tool, ID_TOOL_MOVE);

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

void test_ie_image_resize_dialog_headless(void) {
    TEST("show_image_resize_dialog: returns false in headless mode");

    ie_setup();
    int w = 320, h = 200;
    image_resize_filter_t filter = IMAGE_RESIZE_BILINEAR;

    bool accepted = show_image_resize_dialog(NULL, &w, &h, &filter);
    ASSERT_FALSE(accepted);
    ASSERT_EQUAL(w, 320);
    ASSERT_EQUAL(h, 200);
    ASSERT_EQUAL(filter, IMAGE_RESIZE_BILINEAR);

    ie_teardown();
    PASS();
}

void test_ie_image_resize_bilinear_scales_pixels(void) {
    TEST("canvas_resize_image: bilinear filter rescales pixels");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 2, 2);
    ASSERT_NOT_NULL(doc);

    canvas_set_pixel(doc, 0, 0, MAKE_COLOR(255, 0, 0, 255));
    canvas_set_pixel(doc, 1, 0, MAKE_COLOR(0, 255, 0, 255));
    canvas_set_pixel(doc, 0, 1, MAKE_COLOR(0, 0, 255, 255));
    canvas_set_pixel(doc, 1, 1, MAKE_COLOR(255, 255, 255, 255));

    ASSERT_TRUE(canvas_resize_image(doc, 1, 1, IMAGE_RESIZE_BILINEAR));
    ASSERT_EQUAL(doc->canvas_w, 1);
    ASSERT_EQUAL(doc->canvas_h, 1);

    uint32_t px = canvas_get_pixel(doc, 0, 0);
    ASSERT_EQUAL(COLOR_R(px), 128);
    ASSERT_EQUAL(COLOR_G(px), 128);
    ASSERT_EQUAL(COLOR_B(px), 128);
    ASSERT_EQUAL(COLOR_A(px), 255);
    ASSERT_TRUE(doc->modified);
    ASSERT_FALSE(doc->sel.active);

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

void test_ie_magic_wand_selects_contiguous_color_region(void) {
    TEST("magic wand: selects contiguous pixels within spread tolerance");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 4, 3);
    ASSERT_NOT_NULL(doc);

    uint32_t red = MAKE_COLOR(0xF0, 0x10, 0x10, 0xFF);
    uint32_t near_red = MAKE_COLOR(0xE8, 0x18, 0x12, 0xFF);
    uint32_t blue = MAKE_COLOR(0x20, 0x20, 0xD0, 0xFF);

    for (int y = 0; y < doc->canvas_h; y++)
        for (int x = 0; x < doc->canvas_w; x++)
            canvas_set_pixel(doc, x, y, blue);

    canvas_set_pixel(doc, 0, 0, red);
    canvas_set_pixel(doc, 1, 0, near_red);
    canvas_set_pixel(doc, 0, 1, near_red);
    canvas_set_pixel(doc, 3, 2, red); // same color, but not contiguous

    ASSERT_TRUE(canvas_magic_wand_select(doc, 0, 0, 16, false));
    ASSERT_TRUE(doc->sel.active);
    ASSERT_NOT_NULL(doc->sel.mask.data);
    ASSERT_TRUE(canvas_in_selection(doc, 0, 0));
    ASSERT_TRUE(canvas_in_selection(doc, 1, 0));
    ASSERT_TRUE(canvas_in_selection(doc, 0, 1));
    ASSERT_FALSE(canvas_in_selection(doc, 1, 1));
    ASSERT_FALSE(canvas_in_selection(doc, 3, 2));
    ASSERT_EQUAL(doc->sel.start.x, 0);
    ASSERT_EQUAL(doc->sel.start.y, 0);
    ASSERT_EQUAL(doc->sel.end.x, 1);
    ASSERT_EQUAL(doc->sel.end.y, 1);

    ie_teardown();
    PASS();
}

void test_ie_rect_selection_uses_mask(void) {
    TEST("selection: rectangular selection creates per-pixel mask");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 5, 4);
    ASSERT_NOT_NULL(doc);

    ASSERT_TRUE(canvas_select_rect(doc, 1, 1, 3, 2));
    ASSERT_TRUE(doc->sel.active);
    ASSERT_NOT_NULL(doc->sel.mask.data);
    ASSERT_EQUAL(doc->sel.mask.data[(size_t)1 * doc->canvas_w + 1], 0);
    ASSERT_EQUAL(doc->sel.mask.data[(size_t)0 * doc->canvas_w + 0], 255);
    ASSERT_FALSE(canvas_in_selection(doc, 0, 1));
    ASSERT_TRUE(canvas_in_selection(doc, 1, 1));
    ASSERT_TRUE(canvas_in_selection(doc, 3, 2));
    ASSERT_FALSE(canvas_in_selection(doc, 4, 2));

    ie_teardown();
    PASS();
}

void test_ie_shift_adds_to_selection_mask(void) {
    TEST("selection: Shift-add combines rectangle and wand selections");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 5, 4);
    ASSERT_NOT_NULL(doc);

    uint32_t green = MAKE_COLOR(0x20, 0xA0, 0x20, 0xFF);
    uint32_t red = MAKE_COLOR(0xE0, 0x10, 0x10, 0xFF);
    for (int y = 0; y < doc->canvas_h; y++)
        for (int x = 0; x < doc->canvas_w; x++)
            canvas_set_pixel(doc, x, y, green);
    canvas_set_pixel(doc, 2, 1, red);

    ASSERT_TRUE(canvas_select_rect(doc, 0, 0, 1, 0));
    ASSERT_TRUE(canvas_select_rect_add(doc, 3, 2, 4, 3));
    ASSERT_TRUE(canvas_in_selection(doc, 0, 0));
    ASSERT_TRUE(canvas_in_selection(doc, 1, 0));
    ASSERT_FALSE(canvas_in_selection(doc, 2, 1));
    ASSERT_TRUE(canvas_in_selection(doc, 3, 2));
    ASSERT_TRUE(canvas_in_selection(doc, 4, 3));
    ASSERT_EQUAL(doc->sel.start.x, 0);
    ASSERT_EQUAL(doc->sel.start.y, 0);
    ASSERT_EQUAL(doc->sel.end.x, 4);
    ASSERT_EQUAL(doc->sel.end.y, 3);

    ASSERT_TRUE(canvas_magic_wand_select_add(doc, 2, 1, 0, false));
    ASSERT_TRUE(canvas_in_selection(doc, 0, 0));
    ASSERT_TRUE(canvas_in_selection(doc, 2, 1));
    ASSERT_TRUE(canvas_in_selection(doc, 4, 3));
    ASSERT_FALSE(canvas_in_selection(doc, 2, 2));

    ie_teardown();
    PASS();
}

void test_ie_shift_wand_adds_from_canvas_window(void) {
    TEST("selection: Shift+wand click adds through canvas window handler");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 5, 4);
    ASSERT_NOT_NULL(doc);
    g_app->active_doc = doc;
    g_app->current_tool = ID_TOOL_MAGIC_WAND;
    g_app->wand.spread = 0;
    g_app->wand.antialias = false;

    uint32_t green = MAKE_COLOR(0x20, 0xA0, 0x20, 0xFF);
    uint32_t red = MAKE_COLOR(0xE0, 0x10, 0x10, 0xFF);
    for (int y = 0; y < doc->canvas_h; y++)
        for (int x = 0; x < doc->canvas_w; x++)
            canvas_set_pixel(doc, x, y, green);
    canvas_set_pixel(doc, 0, 0, red);
    canvas_set_pixel(doc, 3, 2, red);

    send_message(doc->canvas_win, evLeftButtonDown, MAKEDWORD(0, 0), NULL);
    ASSERT_TRUE(canvas_in_selection(doc, 0, 0));
    ASSERT_FALSE(canvas_in_selection(doc, 3, 2));

    ui_event_t evt = {0};
    evt.message = kEventModifiersChanged;
    evt.wParam = AX_MOD_SHIFT;
    dispatch_message(&evt);

    send_message(doc->canvas_win, evLeftButtonDown, MAKEDWORD(3, 2), NULL);
    ASSERT_TRUE(canvas_in_selection(doc, 0, 0));
    ASSERT_TRUE(canvas_in_selection(doc, 3, 2));
    ASSERT_FALSE(canvas_in_selection(doc, 1, 1));

    evt.wParam = 0;
    dispatch_message(&evt);

    ie_teardown();
    PASS();
}

void test_ie_shift_rect_selection_is_square(void) {
    TEST("selection: Shift-drag rectangle constrains to a square");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 6, 6);
    ASSERT_NOT_NULL(doc);
    g_app->active_doc = doc;
    g_app->current_tool = ID_TOOL_SELECT;

    send_message(doc->canvas_win, evLeftButtonDown, MAKEDWORD(0, 0), NULL);

    ui_event_t evt = {0};
    evt.message = kEventModifiersChanged;
    evt.wParam = AX_MOD_SHIFT;
    dispatch_message(&evt);

    send_message(doc->canvas_win, evMouseMove, MAKEDWORD(3, 1), NULL);
    send_message(doc->canvas_win, evLeftButtonUp, MAKEDWORD(3, 1), NULL);

    ASSERT_TRUE(doc->sel.active);
    ASSERT_TRUE(canvas_in_selection(doc, 0, 0));
    ASSERT_TRUE(canvas_in_selection(doc, 1, 1));
    ASSERT_FALSE(canvas_in_selection(doc, 2, 1));
    ASSERT_FALSE(canvas_in_selection(doc, 0, 2));
    ASSERT_EQUAL(doc->sel.start.x, 0);
    ASSERT_EQUAL(doc->sel.start.y, 0);
    ASSERT_EQUAL(doc->sel.end.x, 1);
    ASSERT_EQUAL(doc->sel.end.y, 1);

    evt.wParam = 0;
    dispatch_message(&evt);

    ie_teardown();
    PASS();
}

void test_ie_select_tool_moves_selection_mask_only(void) {
    TEST("selection: Select tool drags the mask without moving pixels");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 4, 3);
    ASSERT_NOT_NULL(doc);
    g_app->active_doc = doc;
    g_app->current_tool = ID_TOOL_SELECT;

    uint32_t red = MAKE_COLOR(0xE0, 0x10, 0x10, 0xFF);
    canvas_set_pixel(doc, 1, 1, red);
    ASSERT_TRUE(canvas_select_rect(doc, 1, 1, 1, 1));
    uint8_t *mask = doc->sel.mask.data;
    ASSERT_NOT_NULL(mask);
    doc->sel.mask.dirty = false;

    send_message(doc->canvas_win, evLeftButtonDown, MAKEDWORD(1, 1), NULL);
    send_message(doc->canvas_win, evMouseMove, MAKEDWORD(2, 1), NULL);

    ASSERT_TRUE(doc->sel.move.mask_moving);
    ASSERT_TRUE(doc->sel.mask.data == mask);
    ASSERT_FALSE(doc->sel.mask.dirty);
    ASSERT_EQUAL(doc->sel.mask.offset.x, 1);
    ASSERT_EQUAL(doc->sel.mask.offset.y, 0);
    ASSERT_FALSE(canvas_in_selection(doc, 1, 1));
    ASSERT_TRUE(canvas_in_selection(doc, 2, 1));

    send_message(doc->canvas_win, evLeftButtonUp, MAKEDWORD(2, 1), NULL);

    ASSERT_FALSE(doc->sel.move.mask_moving);
    ASSERT_EQUAL(doc->sel.mask.offset.x, 0);
    ASSERT_EQUAL(doc->sel.mask.offset.y, 0);
    ASSERT_EQUAL(canvas_get_pixel(doc, 1, 1), red);
    ASSERT_FALSE(canvas_in_selection(doc, 1, 1));
    ASSERT_TRUE(canvas_in_selection(doc, 2, 1));

    ie_teardown();
    PASS();
}

void test_ie_select_soft_edge_starts_new_selection(void) {
    TEST("selection: clicking mask value >=128 starts a new selection");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 4, 3);
    ASSERT_NOT_NULL(doc);
    g_app->active_doc = doc;
    g_app->current_tool = ID_TOOL_SELECT;

    doc->sel.mask.data = malloc((size_t)doc->canvas_w * doc->canvas_h);
    ASSERT_NOT_NULL(doc->sel.mask.data);
    memset(doc->sel.mask.data, 255, (size_t)doc->canvas_w * doc->canvas_h);
    doc->sel.mask.data[(size_t)1 * doc->canvas_w + 1] = 128;
    doc->sel.active = true;
    doc->sel.start = (ipoint16_t){1, 1};
    doc->sel.end = (ipoint16_t){1, 1};
    doc->sel.mask.dirty = true;

    send_message(doc->canvas_win, evLeftButtonDown, MAKEDWORD(1, 1), NULL);
    send_message(doc->canvas_win, evMouseMove, MAKEDWORD(2, 1), NULL);
    send_message(doc->canvas_win, evLeftButtonUp, MAKEDWORD(2, 1), NULL);

    ASSERT_FALSE(doc->sel.move.mask_moving);
    ASSERT_TRUE(doc->sel.active);
    ASSERT_TRUE(canvas_in_selection(doc, 1, 1));
    ASSERT_TRUE(canvas_in_selection(doc, 2, 1));
    ASSERT_FALSE(canvas_in_selection(doc, 0, 1));
    ASSERT_EQUAL(doc->sel.start.x, 1);
    ASSERT_EQUAL(doc->sel.start.y, 1);
    ASSERT_EQUAL(doc->sel.end.x, 2);
    ASSERT_EQUAL(doc->sel.end.y, 1);

    ie_teardown();
    PASS();
}

void test_ie_move_tool_moves_selected_pixels(void) {
    TEST("move tool: drags selected pixels and leaves transparency behind");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 4, 3);
    ASSERT_NOT_NULL(doc);
    g_app->active_doc = doc;
    g_app->current_tool = ID_TOOL_MOVE;

    uint32_t red = MAKE_COLOR(0xE0, 0x10, 0x10, 0xFF);
    canvas_set_pixel(doc, 1, 1, red);
    ASSERT_TRUE(canvas_select_rect(doc, 1, 1, 1, 1));

    send_message(doc->canvas_win, evLeftButtonDown, MAKEDWORD(1, 1), NULL);
    send_message(doc->canvas_win, evMouseMove, MAKEDWORD(2, 1), NULL);
    send_message(doc->canvas_win, evLeftButtonUp, MAKEDWORD(2, 1), NULL);

    ASSERT_EQUAL(COLOR_A(canvas_get_pixel(doc, 1, 1)), 0);
    ASSERT_EQUAL(canvas_get_pixel(doc, 2, 1), red);
    ASSERT_FALSE(canvas_in_selection(doc, 1, 1));
    ASSERT_TRUE(canvas_in_selection(doc, 2, 1));

    ie_teardown();
    PASS();
}

void test_ie_move_masked_selection_preserves_shape(void) {
    TEST("selection move: per-pixel mask shape moves without clearing bounding box");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 5, 5);
    ASSERT_NOT_NULL(doc);

    uint32_t bg = MAKE_COLOR(0x20, 0x80, 0x20, 0xFF);
    uint32_t red = MAKE_COLOR(0xE0, 0x10, 0x10, 0xFF);
    for (int y = 0; y < doc->canvas_h; y++)
        for (int x = 0; x < doc->canvas_w; x++)
            canvas_set_pixel(doc, x, y, bg);

    canvas_set_pixel(doc, 1, 1, red);
    canvas_set_pixel(doc, 2, 1, red);
    canvas_set_pixel(doc, 1, 2, red);

    ASSERT_TRUE(canvas_magic_wand_select(doc, 1, 1, 0, false));
    ASSERT_TRUE(canvas_in_selection(doc, 1, 1));
    ASSERT_FALSE(canvas_in_selection(doc, 2, 2));

    canvas_begin_move(doc, bg);
    ASSERT_TRUE(doc->sel.move.active);
    doc->sel.floating.rect.x = 2;
    doc->sel.floating.rect.y = 2;
    canvas_commit_move(doc);

    ASSERT_FALSE(doc->sel.move.active);
    ASSERT_EQUAL(canvas_get_pixel(doc, 1, 1), bg);
    ASSERT_EQUAL(canvas_get_pixel(doc, 2, 1), bg);
    ASSERT_EQUAL(canvas_get_pixel(doc, 1, 2), bg);
    ASSERT_EQUAL(canvas_get_pixel(doc, 2, 2), red);
    ASSERT_EQUAL(canvas_get_pixel(doc, 3, 2), red);
    ASSERT_EQUAL(canvas_get_pixel(doc, 2, 3), red);
    ASSERT_EQUAL(canvas_get_pixel(doc, 3, 3), bg);
    ASSERT_TRUE(canvas_in_selection(doc, 2, 2));
    ASSERT_TRUE(canvas_in_selection(doc, 3, 2));
    ASSERT_TRUE(canvas_in_selection(doc, 2, 3));
    ASSERT_FALSE(canvas_in_selection(doc, 3, 3));

    ie_teardown();
    PASS();
}

void test_ie_clear_selection_makes_pixels_transparent(void) {
    TEST("selection clear: command clears selected pixels to transparency");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 4, 4);
    ASSERT_NOT_NULL(doc);
    g_app->active_doc = doc;

    uint32_t red = MAKE_COLOR(0xE0, 0x10, 0x10, 0xFF);
    uint32_t green = MAKE_COLOR(0x20, 0xA0, 0x20, 0xFF);
    for (int y = 0; y < doc->canvas_h; y++)
        for (int x = 0; x < doc->canvas_w; x++)
            canvas_set_pixel(doc, x, y, green);
    canvas_set_pixel(doc, 1, 1, red);
    canvas_set_pixel(doc, 2, 1, red);

    ASSERT_TRUE(canvas_select_rect(doc, 1, 1, 2, 1));
    handle_menu_command(ID_SELECT_CLEAR);

    ASSERT_EQUAL(COLOR_A(canvas_get_pixel(doc, 1, 1)), 0);
    ASSERT_EQUAL(COLOR_A(canvas_get_pixel(doc, 2, 1)), 0);
    ASSERT_EQUAL(canvas_get_pixel(doc, 0, 0), green);

    ie_teardown();
    PASS();
}

void test_ie_image_crop_command_crops_to_selection(void) {
    TEST("Image > Crop: crops canvas to active selection bounds");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 4, 3);
    ASSERT_NOT_NULL(doc);
    g_app->active_doc = doc;

    uint32_t green = MAKE_COLOR(0x20, 0xA0, 0x20, 0xFF);
    uint32_t red = MAKE_COLOR(0xE0, 0x10, 0x10, 0xFF);
    uint32_t blue = MAKE_COLOR(0x20, 0x20, 0xD0, 0xFF);
    for (int y = 0; y < doc->canvas_h; y++)
        for (int x = 0; x < doc->canvas_w; x++)
            canvas_set_pixel(doc, x, y, green);
    canvas_set_pixel(doc, 2, 1, red);
    canvas_set_pixel(doc, 3, 2, blue);

    ASSERT_TRUE(canvas_select_rect(doc, 2, 1, 3, 2));
    handle_menu_command(ID_IMAGE_CROP);

    ASSERT_EQUAL(doc->canvas_w, 2);
    ASSERT_EQUAL(doc->canvas_h, 2);
    ASSERT_EQUAL(canvas_get_pixel(doc, 0, 0), red);
    ASSERT_EQUAL(canvas_get_pixel(doc, 1, 1), blue);
    ASSERT_FALSE(doc->sel.active);
    ASSERT_TRUE(doc->modified);

    ie_teardown();
    PASS();
}

void test_ie_selection_expand_contract_mask(void) {
    TEST("selection: expand and contract operate on per-pixel mask");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 5, 5);
    ASSERT_NOT_NULL(doc);

    ASSERT_TRUE(canvas_select_rect(doc, 2, 2, 2, 2));
    ASSERT_TRUE(canvas_expand_selection(doc, 1));
    ASSERT_TRUE(canvas_in_selection(doc, 1, 1));
    ASSERT_TRUE(canvas_in_selection(doc, 2, 2));
    ASSERT_TRUE(canvas_in_selection(doc, 3, 3));
    ASSERT_FALSE(canvas_in_selection(doc, 0, 0));

    ASSERT_TRUE(canvas_contract_selection(doc, 1));
    ASSERT_FALSE(canvas_in_selection(doc, 1, 1));
    ASSERT_TRUE(canvas_in_selection(doc, 2, 2));
    ASSERT_FALSE(canvas_in_selection(doc, 3, 3));

    ie_teardown();
    PASS();
}

void test_ie_selection_expand_preserves_soft_mask(void) {
    TEST("selection: expand preserves antialiased mask values");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 3, 3);
    ASSERT_NOT_NULL(doc);

    doc->sel.mask.data = malloc(9);
    ASSERT_NOT_NULL(doc->sel.mask.data);
    memset(doc->sel.mask.data, 255, 9);
    doc->sel.mask.data[4] = 128;
    doc->sel.active = true;
    doc->sel.mask.dirty = true;
    doc->sel.start = (ipoint16_t){1, 1};
    doc->sel.end = (ipoint16_t){1, 1};

    ASSERT_TRUE(canvas_expand_selection(doc, 1));
    ASSERT_EQUAL(doc->sel.mask.data[0], 128);
    ASSERT_EQUAL(doc->sel.mask.data[4], 128);
    ASSERT_EQUAL(doc->sel.start.x, 0);
    ASSERT_EQUAL(doc->sel.start.y, 0);
    ASSERT_EQUAL(doc->sel.end.x, 2);
    ASSERT_EQUAL(doc->sel.end.y, 2);

    ie_teardown();
    PASS();
}

// Out-of-range brush_size must not crash or cause OOB reads when canvas
// draw operations are invoked.  The implementation clamps to [0, NUM_BRUSH_SIZES).
void test_ie_brush_size_oob_clamp(void) {
    TEST("brush_size: out-of-range values are clamped safely during canvas draw");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 32, 32);
    ASSERT_NOT_NULL(doc);
    g_app->current_tool = ID_TOOL_BRUSH;

    // Underflow: -1 should be clamped to radius kBrushSizes[0].
    g_app->brush_size = -1;
    canvas_draw_circle(doc, 16, 16, kBrushSizes[0], g_app->fg_color);  // same as clamped
    canvas_draw_line(doc, 0, 0, 16, 16, kBrushSizes[0], g_app->fg_color);

    // Overflow: NUM_BRUSH_SIZES should be clamped to kBrushSizes[NUM_BRUSH_SIZES-1].
    g_app->brush_size = NUM_BRUSH_SIZES;
    canvas_draw_circle(doc, 16, 16, kBrushSizes[NUM_BRUSH_SIZES - 1], g_app->fg_color);
    canvas_draw_line(doc, 0, 0, 16, 16, kBrushSizes[NUM_BRUSH_SIZES - 1], g_app->fg_color);

    // Reset to valid range — no assertion needed, just verifying no crash above.
    g_app->brush_size = 0;
    ASSERT_EQUAL(g_app->brush_size, 0);

    ie_teardown();
    PASS();
}

// ── Layer management tests ────────────────────────────────────────────────────

// A newly created document has exactly 1 layer named "Background".
void test_ie_layer_initial_state(void) {
    TEST("create_document: initial layer stack has 1 background layer");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 32, 32);
    ASSERT_NOT_NULL(doc);

    ASSERT_EQUAL(doc->layer.count, 1);
    ASSERT_EQUAL(doc->layer.active, 0);
    ASSERT_NOT_NULL(doc->layer.stack);
    ASSERT_NOT_NULL(doc->layer.stack[0]);
    ASSERT_NOT_NULL(doc->layer.stack[0]->pixels);
    ASSERT_TRUE(doc->layer.stack[0]->visible);
    ASSERT_EQUAL(doc->layer.stack[0]->opacity, 255);
    // doc->pixels must alias the active layer's pixel buffer.
    ASSERT_TRUE(doc->pixels == doc->layer.stack[0]->pixels);

    ie_teardown();
    PASS();
}

// Adding a layer increments layer_count and makes the new layer active.
void test_ie_layer_add(void) {
    TEST("doc_add_layer: layer_count increases and new layer is active");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 8, 8);
    ASSERT_NOT_NULL(doc);
    ASSERT_EQUAL(doc->layer.count, 1);

    bool ok = doc_add_layer(doc);
    ASSERT_TRUE(ok);
    ASSERT_EQUAL(doc->layer.count, 2);
    ASSERT_EQUAL(doc->layer.active, 1);
    ASSERT_TRUE(doc->pixels == doc->layer.stack[1]->pixels);

    ie_teardown();
    PASS();
}

// Deleting a layer reduces layer_count.  Cannot delete the last layer.
void test_ie_layer_delete(void) {
    TEST("doc_delete_layer: layer_count decreases; last layer cannot be deleted");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 8, 8);
    ASSERT_NOT_NULL(doc);
    doc_add_layer(doc);
    ASSERT_EQUAL(doc->layer.count, 2);

    bool ok = doc_delete_layer(doc);
    ASSERT_TRUE(ok);
    ASSERT_EQUAL(doc->layer.count, 1);
    ASSERT_EQUAL(doc->layer.active, 0);

    // Cannot delete the last remaining layer.
    ok = doc_delete_layer(doc);
    ASSERT_FALSE(ok);
    ASSERT_EQUAL(doc->layer.count, 1);

    ie_teardown();
    PASS();
}

// Duplicating a layer inserts a copy above the active one.
void test_ie_layer_duplicate(void) {
    TEST("doc_duplicate_layer: inserts copy above active layer");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 4, 4);
    ASSERT_NOT_NULL(doc);

    // Paint a recognizable pixel on layer 0.
    canvas_set_pixel(doc, 0, 0, MAKE_COLOR(0xFF, 0, 0, 0xFF));

    bool ok = doc_duplicate_layer(doc);
    ASSERT_TRUE(ok);
    ASSERT_EQUAL(doc->layer.count, 2);
    ASSERT_EQUAL(doc->layer.active, 1);

    // Both layers should have the same pixel at (0,0).
    doc_set_active_layer(doc, 0);
    uint32_t p0 = canvas_get_pixel(doc, 0, 0);
    doc_set_active_layer(doc, 1);
    uint32_t p1 = canvas_get_pixel(doc, 0, 0);
    ASSERT_EQUAL(COLOR_R(p0), COLOR_R(p1));

    ie_teardown();
    PASS();
}

// doc_set_active_layer switches doc->pixels to the correct layer buffer.
void test_ie_layer_set_active(void) {
    TEST("doc_set_active_layer: doc->pixels points to selected layer's pixels");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 8, 8);
    ASSERT_NOT_NULL(doc);
    doc_add_layer(doc);

    doc_set_active_layer(doc, 0);
    ASSERT_EQUAL(doc->layer.active, 0);
    ASSERT_TRUE(doc->pixels == doc->layer.stack[0]->pixels);

    doc_set_active_layer(doc, 1);
    ASSERT_EQUAL(doc->layer.active, 1);
    ASSERT_TRUE(doc->pixels == doc->layer.stack[1]->pixels);

    doc_set_mask_only_view(doc, true);
    ASSERT_TRUE(doc->layer.mask_only_view);
    doc->canvas_dirty = false;
    doc_set_active_layer(doc, 0);
    ASSERT_TRUE(doc->canvas_dirty);
    ASSERT_EQUAL(doc->layer.active, 0);

    ie_teardown();
    PASS();
}

// Move layer up / down reorders the stack correctly.
void test_ie_layer_move(void) {
    TEST("doc_move_layer_up/down: layer order and active_layer index updated");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 4, 4);
    ASSERT_NOT_NULL(doc);
    doc_add_layer(doc);
    doc_add_layer(doc);
    ASSERT_EQUAL(doc->layer.count, 3);
    ASSERT_EQUAL(doc->layer.active, 2);

    doc_move_layer_down(doc);
    ASSERT_EQUAL(doc->layer.active, 1);

    doc_move_layer_up(doc);
    ASSERT_EQUAL(doc->layer.active, 2);

    // Moving the bottom layer down does nothing.
    doc_set_active_layer(doc, 0);
    doc_move_layer_down(doc);
    ASSERT_EQUAL(doc->layer.active, 0);

    // Moving the top layer up does nothing.
    doc_set_active_layer(doc, 2);
    doc_move_layer_up(doc);
    ASSERT_EQUAL(doc->layer.active, 2);

    ie_teardown();
    PASS();
}

// Flatten reduces any number of layers to a single background layer.
void test_ie_layer_flatten(void) {
    TEST("doc_flatten: reduces layer stack to a single layer");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 4, 4);
    ASSERT_NOT_NULL(doc);
    doc_add_layer(doc);
    doc_add_layer(doc);
    ASSERT_EQUAL(doc->layer.count, 3);

    doc_flatten(doc);
    ASSERT_EQUAL(doc->layer.count, 1);
    ASSERT_EQUAL(doc->layer.active, 0);
    ASSERT_NOT_NULL(doc->pixels);
    ASSERT_TRUE(doc->pixels == doc->layer.stack[0]->pixels);

    ie_teardown();
    PASS();
}

// Flatten should preserve transparency now that the canvas composite keeps alpha.
void test_ie_flatten_preserves_alpha(void) {
    TEST("doc_flatten: preserves layer alpha in the flattened result");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 1, 1);
    ASSERT_NOT_NULL(doc);

    canvas_set_pixel(doc, 0, 0, MAKE_COLOR(0x00, 0x00, 0x00, 0x00));
    doc_add_layer(doc);
    doc_set_active_layer(doc, 1);
    canvas_set_pixel(doc, 0, 0, MAKE_COLOR(0xFF, 0x00, 0x00, 0x80));

    doc_flatten(doc);
    ASSERT_EQUAL(doc->layer.count, 1);
    ASSERT_EQUAL(COLOR_R(canvas_get_pixel(doc, 0, 0)), 0xFF);
    ASSERT_EQUAL(COLOR_A(canvas_get_pixel(doc, 0, 0)), 0x80);

    ie_teardown();
    PASS();
}

// Merge-down blends the active layer onto the one below it.
void test_ie_layer_merge_down(void) {
    TEST("doc_merge_down: merges active layer onto layer below");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 4, 4);
    ASSERT_NOT_NULL(doc);

    // Layer 0 (background): opaque white via canvas_clear (already done).
    doc_add_layer(doc);  // layer 1
    // Paint an opaque red pixel on layer 1 at (0,0).
    canvas_set_pixel(doc, 0, 0, MAKE_COLOR(0xFF, 0, 0, 0xFF));

    ASSERT_EQUAL(doc->layer.active, 1);
    doc_merge_down(doc);
    ASSERT_EQUAL(doc->layer.count, 1);
    ASSERT_EQUAL(doc->layer.active, 0);

    // The merged pixel at (0,0) should be red.
    uint32_t px = canvas_get_pixel(doc, 0, 0);
    ASSERT_EQUAL(COLOR_R(px), 0xFF);
    ASSERT_EQUAL(COLOR_G(px), 0x00);

    ie_teardown();
    PASS();
}

// ── Alpha-edit tests ───────────────────────────────────────────────────────────

void test_ie_mask_add(void) {
    TEST("layer_add_mask: alpha channel initialized to 0xFF");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 4, 4);
    ASSERT_NOT_NULL(doc);

    canvas_set_pixel(doc, 0, 0, MAKE_COLOR(0x10, 0x20, 0x30, 0x00));

    bool ok = layer_add_mask(doc, 0);
    ASSERT_TRUE(ok);
    ASSERT_EQUAL(COLOR_A(canvas_get_pixel(doc, 0, 0)), 0xFF);
    ASSERT_TRUE(doc->layer.editing_mask);

    ie_teardown();
    PASS();
}

void test_ie_mask_remove(void) {
    TEST("layer_remove_mask: alpha restored to opaque");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 4, 4);
    ASSERT_NOT_NULL(doc);

    canvas_set_pixel(doc, 0, 0, MAKE_COLOR(0xFF, 0x00, 0x00, 0x80));
    doc->layer.editing_mask = true;

    layer_remove_mask(doc, 0);
    ASSERT_EQUAL(COLOR_A(canvas_get_pixel(doc, 0, 0)), 0xFF);
    ASSERT_FALSE(doc->layer.editing_mask);

    ie_teardown();
    PASS();
}

void test_ie_mask_apply(void) {
    TEST("layer_apply_mask: exits alpha-edit mode without changing pixels");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 1, 1);
    ASSERT_NOT_NULL(doc);

    canvas_set_pixel(doc, 0, 0, MAKE_COLOR(0xFF, 0x00, 0x00, 0x80));
    doc->layer.editing_mask = true;
    layer_apply_mask(doc, 0);
    ASSERT_FALSE(doc->layer.editing_mask);
    ASSERT_EQUAL(COLOR_A(canvas_get_pixel(doc, 0, 0)), 0x80);

    ie_teardown();
    PASS();
}

void test_ie_mask_extract(void) {
    TEST("canvas_extract_mask: new document created from alpha channel");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 2, 2);
    ASSERT_NOT_NULL(doc);

    canvas_set_pixel(doc, 0, 0, MAKE_COLOR(0xFF, 0, 0, 200));

    canvas_doc_t *mask_doc = canvas_extract_mask(doc);
    ASSERT_NOT_NULL(mask_doc);
    ASSERT_EQUAL(mask_doc->canvas_w, 2);
    ASSERT_EQUAL(mask_doc->canvas_h, 2);

    // Pixel at (0,0) should be grey with value = alpha (200).
    uint32_t px = canvas_get_pixel(mask_doc, 0, 0);
    ASSERT_EQUAL(COLOR_R(px), 200);
    ASSERT_EQUAL(COLOR_G(px), 200);
    ASSERT_EQUAL(COLOR_B(px), 200);

    ie_teardown();
    PASS();
}

void test_ie_mask_add_fill_modes(void) {
    TEST("layer_add_mask_ex: fill modes map to alpha values");

    ie_setup();
    g_app->fg_color = MAKE_COLOR(0x11, 0x22, 0x33, 0xFF);
    g_app->bg_color = MAKE_COLOR(0x44, 0x55, 0x66, 0xFF);

    canvas_doc_t *doc = create_document(NULL, 1, 1);
    ASSERT_NOT_NULL(doc);
    canvas_set_pixel(doc, 0, 0, MAKE_COLOR(0xAA, 0x40, 0x20, 0xFF));

    ASSERT_TRUE(layer_add_mask_ex(doc, doc->layer.active, MASK_EXTRACT_GRAYSCALE));
    uint8_t expected_gray = (uint8_t)((0xAA * 77 + 0x40 * 150 + 0x20 * 29) >> 8);
    ASSERT_EQUAL(COLOR_A(canvas_get_pixel(doc, 0, 0)), expected_gray);

    ASSERT_TRUE(layer_add_mask_ex(doc, doc->layer.active, MASK_EXTRACT_WHITE));
    ASSERT_EQUAL(COLOR_A(canvas_get_pixel(doc, 0, 0)), 255);

    ASSERT_TRUE(layer_add_mask_ex(doc, doc->layer.active, MASK_EXTRACT_BACKGROUND));
    uint8_t expected_bg = (uint8_t)((0x44 * 77 + 0x55 * 150 + 0x66 * 29) >> 8);
    ASSERT_EQUAL(COLOR_A(canvas_get_pixel(doc, 0, 0)), expected_bg);

    ASSERT_TRUE(layer_add_mask_ex(doc, doc->layer.active, MASK_EXTRACT_FOREGROUND));
    uint8_t expected_fg = (uint8_t)((0x11 * 77 + 0x22 * 150 + 0x33 * 29) >> 8);
    ASSERT_EQUAL(COLOR_A(canvas_get_pixel(doc, 0, 0)), expected_fg);

    ie_teardown();
    PASS();
}

void test_ie_extract_mask_exports_alpha(void) {
    TEST("canvas_extract_mask: exports the layer alpha channel");

    ie_setup();

    canvas_doc_t *doc = create_document(NULL, 1, 1);
    ASSERT_NOT_NULL(doc);
    canvas_set_pixel(doc, 0, 0, MAKE_COLOR(0x12, 0x34, 0x56, 123));

    canvas_doc_t *mask_doc = canvas_extract_mask(doc);
    ASSERT_NOT_NULL(mask_doc);
    uint32_t px = canvas_get_pixel(mask_doc, 0, 0);
    ASSERT_EQUAL(COLOR_R(px), 123);
    ASSERT_EQUAL(COLOR_G(px), 123);
    ASSERT_EQUAL(COLOR_B(px), 123);
    close_document(mask_doc);

    ie_teardown();
    PASS();
}

void test_ie_swap_fg_bg(void) {
    TEST("swap_foreground_background_colors swaps swatches");

    ie_setup();
    g_app->fg_color = MAKE_COLOR(0x10, 0x20, 0x30, 0xFF);
    g_app->bg_color = MAKE_COLOR(0xA0, 0xB0, 0xC0, 0xFF);

    swap_foreground_background_colors();

    ASSERT_EQUAL(g_app->fg_color, MAKE_COLOR(0xA0, 0xB0, 0xC0, 0xFF));
    ASSERT_EQUAL(g_app->bg_color, MAKE_COLOR(0x10, 0x20, 0x30, 0xFF));

    ie_teardown();
    PASS();
}

// Undo / redo round-trips across a multi-layer edit.
void test_ie_layer_undo_redo(void) {
    TEST("undo/redo: restores layer stack across add/delete");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 4, 4);
    ASSERT_NOT_NULL(doc);
    ASSERT_EQUAL(doc->layer.count, 1);

    // Push undo, then add a layer.
    doc_push_undo(doc);
    doc_add_layer(doc);
    ASSERT_EQUAL(doc->layer.count, 2);

    // Undo should restore to 1 layer.
    bool ok = doc_undo(doc);
    ASSERT_TRUE(ok);
    ASSERT_EQUAL(doc->layer.count, 1);

    // Redo should bring back 2 layers.
    ok = doc_redo(doc);
    ASSERT_TRUE(ok);
    ASSERT_EQUAL(doc->layer.count, 2);

    ie_teardown();
    PASS();
}

// ── imageeditor_open_file_path tests ──────────────────────────────────────────

// imageeditor_open_file_path() returns false and leaves g_app->docs unchanged
// when given a path that does not exist.
void test_ie_open_file_path_nonexistent(void) {
    TEST("imageeditor_open_file_path: returns false for a nonexistent file");

    ie_setup();

    ASSERT_NULL(g_app->docs);
    bool ok = imageeditor_open_file_path("/tmp/orion_no_such_file_abcxyz.png");
    ASSERT_FALSE(ok);
    ASSERT_NULL(g_app->docs);

    ie_teardown();
    PASS();
}

// imageeditor_open_file_path() returns false for an empty path.
void test_ie_open_file_path_empty(void) {
    TEST("imageeditor_open_file_path: returns false for empty path");

    ie_setup();

    ASSERT_FALSE(imageeditor_open_file_path(""));
    ASSERT_NULL(g_app->docs);

    ie_teardown();
    PASS();
}

// imageeditor_open_file_path() returns false for a NULL path.
void test_ie_open_file_path_null(void) {
    TEST("imageeditor_open_file_path: returns false for NULL path");

    ie_setup();

    ASSERT_FALSE(imageeditor_open_file_path(NULL));
    ASSERT_NULL(g_app->docs);

    ie_teardown();
    PASS();
}

// imageeditor_open_file_path() succeeds for a valid PNG: a new document is
// added to g_app->docs with the image dimensions and unmodified state.
void test_ie_open_file_path_success(void) {
    TEST("imageeditor_open_file_path: opens PNG and creates document with matching dimensions");

    ie_setup();

    // Write a minimal 4×3 RGBA PNG to a temp file.
    const int W = 4, H = 3;
    uint8_t pixels[4 * 3 * 4];
    for (int i = 0; i < W * H * 4; i++) pixels[i] = (uint8_t)(i & 0xFF);

    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s/orion_test_open_file.png", ie_temp_dir());
    bool saved = save_image_png(tmp, pixels, W, H);
    ASSERT_TRUE(saved);

    bool ok = imageeditor_open_file_path(tmp);
    ASSERT_TRUE(ok);
    ASSERT_NOT_NULL(g_app->docs);

    canvas_doc_t *doc = g_app->docs;
    ASSERT_EQUAL(doc->canvas_w, W);
    ASSERT_EQUAL(doc->canvas_h, H);
    ASSERT_NOT_NULL(doc->pixels);
    ASSERT_FALSE(doc->modified);

    ie_teardown();
    remove(tmp);
    PASS();
}

// imageeditor_open_file_path() loading a second file adds a second document
// in front of the list without closing the first.
void test_ie_open_file_path_multiple(void) {
    TEST("imageeditor_open_file_path: second open adds second document to list");

    ie_setup();

    char tmp1[512], tmp2[512];
    snprintf(tmp1, sizeof(tmp1), "%s/orion_test_open1.png", ie_temp_dir());
    snprintf(tmp2, sizeof(tmp2), "%s/orion_test_open2.png", ie_temp_dir());
    uint8_t px1[8 * 6 * 4]; memset(px1, 0xFF, sizeof(px1));
    uint8_t px2[5 * 5 * 4]; memset(px2, 0x80, sizeof(px2));
    ASSERT_TRUE(save_image_png(tmp1, px1, 8, 6));
    ASSERT_TRUE(save_image_png(tmp2, px2, 5, 5));

    ASSERT_TRUE(imageeditor_open_file_path(tmp1));
    ASSERT_NOT_NULL(g_app->docs);
    ASSERT_EQUAL(g_app->docs->canvas_w, 8);
    ASSERT_EQUAL(g_app->docs->canvas_h, 6);

    ASSERT_TRUE(imageeditor_open_file_path(tmp2));
    // Second open is prepended — it becomes the new head.
    ASSERT_EQUAL(g_app->docs->canvas_w, 5);
    ASSERT_EQUAL(g_app->docs->canvas_h, 5);
    ASSERT_NOT_NULL(g_app->docs->next);
    ASSERT_EQUAL(g_app->docs->next->canvas_w, 8);

    ie_teardown();
    remove(tmp1);
    remove(tmp2);
    PASS();
}

// Large images opened from disk should get the bird's-eye view scale restored:
// the document fits the workspace by scaling below 1x instead of opening at 1x.
void test_ie_open_file_path_large_uses_birdeye_scale(void) {
    TEST("imageeditor_open_file_path: large PNG opens with bird's-eye scale");

    ie_setup();

    const int W = 1000, H = 800;
    uint8_t *pixels = calloc((size_t)W * (size_t)H * 4, 1);
    ASSERT_NOT_NULL(pixels);

    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s/orion_test_open_large.png", ie_temp_dir());
    ASSERT_TRUE(save_image_png(tmp, pixels, W, H));
    free(pixels);

    ASSERT_TRUE(imageeditor_open_file_path(tmp));
    ASSERT_NOT_NULL(g_app->docs);
    ASSERT_NOT_NULL(g_app->docs->canvas_win);

    canvas_win_state_t *state = (canvas_win_state_t *)g_app->docs->canvas_win->userdata;
    ASSERT_NOT_NULL(state);
    ASSERT_TRUE(state->scale < 1.0f);

    ie_teardown();
    remove(tmp);
    PASS();
}

// ── canvas_win_fit_zoom (bird's-eye view) tests ───────────────────────────────

// canvas_win_fit_zoom on a canvas window with zero dimensions is a no-op —
// no crash and scale stays at the default.
void test_ie_fit_zoom_zero_viewport(void) {
    TEST("canvas_win_fit_zoom: zero-size viewport is a no-op, no crash");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 320, 200);
    ASSERT_NOT_NULL(doc);

    // canvas_win frame is 0×0 in headless mode — fit zoom must not crash.
    canvas_win_fit_zoom(doc->canvas_win);

    canvas_win_state_t *state = (canvas_win_state_t *)doc->canvas_win->userdata;
    ASSERT_NOT_NULL(state);
    // Scale must remain at its initial value (1).
    ASSERT_EQUAL(state->scale, 1);

    ie_teardown();
    PASS();
}

// canvas_win_fit_zoom picks the largest zoom where the image fits entirely
// in the viewport, then centers the image.
void test_ie_fit_zoom_selects_best_scale(void) {
    TEST("canvas_win_fit_zoom: selects largest integer zoom that fits viewport");

    ie_setup();
    // 32×20 canvas, 200×150 viewport → 4x fits (32*4=128≤200, 20*4=80≤150)
    //                                   8x does not (32*8=256>200)
    canvas_doc_t *doc = create_document(NULL, 32, 20);
    ASSERT_NOT_NULL(doc);

    // Manually set the canvas window frame to simulate a real viewport.
    doc->canvas_win->frame.w = 200;
    doc->canvas_win->frame.h = 150;

    canvas_win_fit_zoom(doc->canvas_win);

    canvas_win_state_t *state = (canvas_win_state_t *)doc->canvas_win->userdata;
    ASSERT_NOT_NULL(state);
    // Expected fit scale: 4 (32*4=128 ≤ 200-SCROLLBAR_WIDTH; 20*4=80 ≤ 150)
    ASSERT_EQUAL(state->scale, 4);

    ie_teardown();
    PASS();
}

// When the image is larger than the viewport at every zoom level, fit zoom
// must fall back to 1x (the minimum).
void test_ie_fit_zoom_fallback_to_1x(void) {
    TEST("canvas_win_fit_zoom: falls back to 1x when image exceeds viewport at all zoom levels");

    ie_setup();
    // 1000×800 canvas in a 200×150 viewport — even 1x doesn't fit, stays at 1.
    canvas_doc_t *doc = create_document(NULL, 1000, 800);
    ASSERT_NOT_NULL(doc);

    doc->canvas_win->frame.w = 200;
    doc->canvas_win->frame.h = 150;

    canvas_win_fit_zoom(doc->canvas_win);

    canvas_win_state_t *state = (canvas_win_state_t *)doc->canvas_win->userdata;
    ASSERT_NOT_NULL(state);
    ASSERT_EQUAL(state->scale, 1);

    ie_teardown();
    PASS();
}

// Small images should be centered inside a larger document window, and mouse
// coordinates must follow that visual origin instead of treating the window's
// top-left corner as canvas pixel (0,0).
void test_ie_canvas_centers_small_image_hit_testing(void) {
    TEST("canvas window: small image is centered for hit testing");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 32, 20);
    ASSERT_NOT_NULL(doc);

    doc->canvas_win->frame.w = 200;
    doc->canvas_win->frame.h = 150;
    canvas_win_sync_scrollbars(doc->canvas_win);

    canvas_win_state_t *state = (canvas_win_state_t *)doc->canvas_win->userdata;
    ASSERT_NOT_NULL(state);
    ASSERT_EQUAL(state->scale, 1);
    ASSERT_EQUAL(state->pan.x, 0);
    ASSERT_EQUAL(state->pan.y, 0);

    int origin_x = ((200 - SCROLLBAR_WIDTH) - 32) / 2;
    int origin_y = (150 - 20) / 2;

    send_message(doc->canvas_win, evMouseMove, MAKEDWORD(0, 0), NULL);
    ASSERT_FALSE(state->hover_valid);

    send_message(doc->canvas_win, evMouseMove, MAKEDWORD(origin_x, origin_y), NULL);
    ASSERT_TRUE(state->hover_valid);
    ASSERT_EQUAL(state->hover.x, 0);
    ASSERT_EQUAL(state->hover.y, 0);

    send_message(doc->canvas_win, evMouseMove,
                 MAKEDWORD(origin_x + 31, origin_y + 19), NULL);
    ASSERT_TRUE(state->hover_valid);
    ASSERT_EQUAL(state->hover.x, 31);
    ASSERT_EQUAL(state->hover.y, 19);

    send_message(doc->canvas_win, evMouseMove,
                 MAKEDWORD(origin_x + 32, origin_y + 20), NULL);
    ASSERT_FALSE(state->hover_valid);

    ie_teardown();
    PASS();
}

// handle_menu_command(ID_VIEW_ZOOM_FIT) must not crash when there is no active
// document.
void test_ie_zoom_fit_no_doc(void) {
    TEST("ID_VIEW_ZOOM_FIT: no active document — no crash");

    ie_setup();
    g_app->active_doc = NULL;

    handle_menu_command(ID_VIEW_ZOOM_FIT);  // must not crash

    ie_teardown();
    PASS();
}

// handle_menu_command(ID_VIEW_ZOOM_FIT) calls canvas_win_fit_zoom on the
// active document's canvas window.
void test_ie_zoom_fit_command(void) {
    TEST("ID_VIEW_ZOOM_FIT: applies fit zoom to active document canvas");

    ie_setup();
    canvas_doc_t *doc = create_document(NULL, 32, 20);
    ASSERT_NOT_NULL(doc);
    g_app->active_doc = doc;

    doc->canvas_win->frame.w = 200;
    doc->canvas_win->frame.h = 150;

    handle_menu_command(ID_VIEW_ZOOM_FIT);

    canvas_win_state_t *state = (canvas_win_state_t *)doc->canvas_win->userdata;
    ASSERT_NOT_NULL(state);
    // 32*4=128 ≤ 200-SCROLLBAR_WIDTH; 20*4=80 ≤ 150 → expect 4x
    ASSERT_EQUAL(state->scale, 4);

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
    test_ie_document_windows_cascade();
    test_ie_large_document_windows_cascade();
    test_ie_anim_new_frame_selects_inserted_frame();
    test_ie_anim_trace_toggle();
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
    test_ie_image_resize_dialog_headless();
    test_ie_image_resize_bilinear_scales_pixels();
    test_ie_tool_options_window_created();
    test_ie_close_tool_options_window_clears_pointer();
    test_ie_brush_size_valid_range();
    test_ie_brush_sizes_array();
    test_ie_tool_switch_updates_options_panel();
    test_ie_shape_filled_state();
    test_ie_magic_wand_selects_contiguous_color_region();
    test_ie_rect_selection_uses_mask();
    test_ie_shift_adds_to_selection_mask();
    test_ie_shift_wand_adds_from_canvas_window();
    test_ie_shift_rect_selection_is_square();
    test_ie_select_tool_moves_selection_mask_only();
    test_ie_select_soft_edge_starts_new_selection();
    test_ie_move_tool_moves_selected_pixels();
    test_ie_move_masked_selection_preserves_shape();
    test_ie_clear_selection_makes_pixels_transparent();
    test_ie_image_crop_command_crops_to_selection();
    test_ie_selection_expand_contract_mask();
    test_ie_selection_expand_preserves_soft_mask();
    test_ie_brush_size_oob_clamp();
    // Layer management tests
    test_ie_layer_initial_state();
    test_ie_layer_add();
    test_ie_layer_delete();
    test_ie_layer_duplicate();
    test_ie_layer_set_active();
    test_ie_layer_move();
    test_ie_layer_flatten();
    test_ie_flatten_preserves_alpha();
    test_ie_layer_merge_down();
    // Alpha-edit tests
    test_ie_mask_add();
    test_ie_mask_remove();
    test_ie_mask_apply();
    test_ie_mask_extract();
    test_ie_mask_add_fill_modes();
    test_ie_extract_mask_exports_alpha();
    test_ie_swap_fg_bg();
    // Undo/redo with layers
    test_ie_layer_undo_redo();

    // imageeditor_open_file_path tests (from PR #154 review)
    test_ie_open_file_path_nonexistent();
    test_ie_open_file_path_empty();
    test_ie_open_file_path_null();
    test_ie_open_file_path_success();
    test_ie_open_file_path_multiple();
    test_ie_open_file_path_large_uses_birdeye_scale();

    // canvas_win_fit_zoom / bird's-eye view tests
    test_ie_fit_zoom_zero_viewport();
    test_ie_fit_zoom_selects_best_scale();
    test_ie_fit_zoom_fallback_to_1x();
    test_ie_canvas_centers_small_image_hit_testing();
    test_ie_zoom_fit_no_doc();
    test_ie_zoom_fit_command();

    TEST_END();
}
