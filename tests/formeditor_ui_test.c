// tests/formeditor_ui_test.c — headless integration tests for the form editor.
//
// Tests CRUD operations on form elements: create (place), read (select),
// update (resize), and delete.  Drives the form editor through the same
// public APIs and message-passing that the real application uses.  No
// display or OpenGL context is required; evPaint is never triggered so
// GL texture handles stay at 0 throughout.
//
// Pattern (mirrors imageeditor_ui_test.c):
//   1. fe_setup  : test_env_init + allocate g_app + create_form_doc.
//   2. Test body : send messages to canvas_win, call helpers, inspect doc.
//   3. fe_teardown: close doc, free g_app, test_env_shutdown.

#include "test_framework.h"
#include "test_env.h"
#include "../examples/formeditor/formeditor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

// ── Application global – defined in main.c (excluded from this build) ─────
app_state_t *g_app = NULL;

// ── Temp directory helper ──────────────────────────────────────────────────
static const char *fe_temp_dir(void) {
    const char *d = getenv("TEMP");
    if (!d) d = getenv("TMP");
    if (!d) d = getenv("TMPDIR");
    if (!d) d = "/tmp";
    return d;
}

// ── Setup / teardown ───────────────────────────────────────────────────────

static void fe_setup(void) {
    // If a previous test exited via FAIL() without reaching fe_teardown(), the
    // global g_app and its document window are still alive.  Clean them up
    // explicitly so test_env_init() finds a consistent state.
    if (g_app) {
        if (g_app->doc)
            close_form_doc(g_app->doc);
        free(g_app);
        g_app = NULL;
    }
    test_env_init();
    g_app = calloc(1, sizeof(app_state_t));
    g_app->current_tool = ID_TOOL_SELECT;
    // hinstance=0 is fine for headless tests; create_form_doc guards on g_app.
    create_form_doc(FORM_DEFAULT_W, FORM_DEFAULT_H);
}

static void fe_teardown(void) {
    if (!g_app) {
        test_env_shutdown();
        return;
    }
    // close_form_doc destroys the window tree and frees the doc struct.
    if (g_app->doc)
        close_form_doc(g_app->doc);
    free(g_app);
    g_app = NULL;
    test_env_shutdown();
}

// ── Canvas interaction helpers ─────────────────────────────────────────────

// Place a new control by simulating a rubber-band drag on the canvas.
// fx, fy, fw, fh are in form coordinates.  Snap must be disabled by the
// caller for predictable geometry (doc->snap_to_grid = false).
static void fe_place_ctrl(form_doc_t *doc, int tool,
                           int fx, int fy, int fw, int fh) {
    window_t *cwin = doc->canvas_win;
    int sx0 = CANVAS_PADDING + fx;
    int sy0 = CANVAS_PADDING + fy;
    int sx1 = CANVAS_PADDING + fx + fw;
    int sy1 = CANVAS_PADDING + fy + fh;
    g_app->current_tool = tool;
    send_message(cwin, evLeftButtonDown, MAKEDWORD(sx0, sy0), NULL);
    send_message(cwin, evMouseMove,      MAKEDWORD(sx1, sy1), NULL);
    send_message(cwin, evLeftButtonUp,   MAKEDWORD(sx1, sy1), NULL);
    // evLeftButtonUp reverts current_tool to ID_TOOL_SELECT after placement.
}

// Start a placement drag and leave it active so tests can inspect the live
// preview before mouse release commits the control.
static void fe_begin_place_drag(form_doc_t *doc, int tool,
                                int fx, int fy, int fw, int fh) {
    window_t *cwin = doc->canvas_win;
    int sx0 = CANVAS_PADDING + fx;
    int sy0 = CANVAS_PADDING + fy;
    int sx1 = CANVAS_PADDING + fx + fw;
    int sy1 = CANVAS_PADDING + fy + fh;
    g_app->current_tool = tool;
    send_message(cwin, evLeftButtonDown, MAKEDWORD(sx0, sy0), NULL);
    send_message(cwin, evMouseMove,      MAKEDWORD(sx1, sy1), NULL);
}

// Click on an element to select it (DOWN then UP with ID_TOOL_SELECT).
static void fe_select(form_doc_t *doc, int elem_idx) {
    form_element_t *el = &doc->elements[elem_idx];
    window_t *cwin = doc->canvas_win;
    // Click one pixel inside the element's top-left corner.
    int sx = CANVAS_PADDING + el->x + 1;
    int sy = CANVAS_PADDING + el->y + 1;
    g_app->current_tool = ID_TOOL_SELECT;
    send_message(cwin, evLeftButtonDown, MAKEDWORD(sx, sy), NULL);
    send_message(cwin, evLeftButtonUp,   MAKEDWORD(sx, sy), NULL);
}

// Simulate a BR-handle resize drag: DOWN on the handle, MOVE, then UP.
// dw and dh are the pixel deltas to add to width and height.
static void fe_resize_br(form_doc_t *doc, int dw, int dh) {
    form_element_t *el = &doc->elements[
        ((canvas_state_t *)doc->canvas_win->userdata)->selected_idx];
    window_t *cwin = doc->canvas_win;
    // BR handle top-left: (CANVAS_PADDING + el->x + el->w - HANDLE_HALF,
    //                       CANVAS_PADDING + el->y + el->h - HANDLE_HALF)
    // HANDLE_HALF = HANDLE_SIZE/2 = 5/2 = 2
    int hx = CANVAS_PADDING + el->x + el->w - 2;
    int hy = CANVAS_PADDING + el->y + el->h - 2;
    send_message(cwin, evLeftButtonDown, MAKEDWORD(hx,      hy),      NULL);
    send_message(cwin, evMouseMove,      MAKEDWORD(hx + dw, hy + dh), NULL);
    send_message(cwin, evLeftButtonUp,   MAKEDWORD(hx + dw, hy + dh), NULL);
}

// Return a pointer to the canvas state for the given document.
static canvas_state_t *fe_state(form_doc_t *doc) {
    return (canvas_state_t *)doc->canvas_win->userdata;
}

static int fe_count_canvas_children_with_proc(form_doc_t *doc, winproc_t proc) {
    int count = 0;
    for (window_t *child = doc->canvas_win->children; child; child = child->next) {
        if (child->proc == proc)
            count++;
    }
    return count;
}

// ── Tests ──────────────────────────────────────────────────────────────────

// A freshly created document has the expected defaults and no elements.
void test_fe_create_doc(void) {
    TEST("create_form_doc: initialises doc with correct defaults");

    fe_setup();
    form_doc_t *doc = g_app->doc;

    ASSERT_NOT_NULL(doc);
    ASSERT_TRUE(is_window(doc->doc_win));     // root window — is_window works
    ASSERT_NOT_NULL(doc->canvas_win);          // child window — use ptr check
    ASSERT_TRUE(doc->canvas_win->parent == doc->doc_win);
    ASSERT_EQUAL(doc->element_count, 0);
    ASSERT_EQUAL(doc->form_w, FORM_DEFAULT_W);
    ASSERT_EQUAL(doc->form_h, FORM_DEFAULT_H);
    ASSERT_FALSE(doc->modified);
    ASSERT_EQUAL(doc->next_id, CTRL_ID_BASE);

    fe_teardown();
    PASS();
}

// close_form_doc frees the struct and nulls g_app->doc; the window is gone.
void test_fe_close_doc(void) {
    TEST("close_form_doc: removes doc from g_app and destroys window");

    fe_setup();
    form_doc_t *doc    = g_app->doc;
    window_t   *dwin   = doc->doc_win;

    close_form_doc(doc);

    ASSERT_NULL(g_app->doc);
    ASSERT_FALSE(is_window(dwin));

    // Prevent double-free in teardown.
    fe_teardown();
    PASS();
}

// Placing a button increases element_count by 1 with the right type.
void test_fe_place_button(void) {
    TEST("place button: element_count=1, type=CTRL_BUTTON");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    fe_place_ctrl(doc, ID_TOOL_BUTTON, 20, 20, 80, 30);

    ASSERT_EQUAL(doc->element_count, 1);
    ASSERT_EQUAL(doc->elements[0].type, CTRL_BUTTON);
    ASSERT_EQUAL(doc->elements[0].x, 20);
    ASSERT_EQUAL(doc->elements[0].y, 20);
    ASSERT_EQUAL(doc->elements[0].w, 80);
    ASSERT_EQUAL(doc->elements[0].h, 30);
    ASSERT_TRUE(doc->modified);

    fe_teardown();
    PASS();
}

// Dragging a new button shows a live command-button preview before release,
// matching the VB designer behavior instead of drawing only a rubber band.
void test_fe_button_preview_visible_while_dragging(void) {
    TEST("place button drag: preview is a live button before mouse release");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    fe_begin_place_drag(doc, ID_TOOL_BUTTON, 20, 20, 80, 30);

    canvas_state_t *s = fe_state(doc);
    window_t *first_preview = s->preview_win;
    ASSERT_EQUAL(doc->element_count, 0);
    ASSERT_NOT_NULL(s->preview_win);
    ASSERT_TRUE(s->preview_win->proc == win_button);
    ASSERT_EQUAL(s->preview_win->frame.x, CANVAS_PADDING + 20);
    ASSERT_EQUAL(s->preview_win->frame.y, CANVAS_PADDING + 20);
    ASSERT_EQUAL(s->preview_win->frame.w, 80);
    ASSERT_EQUAL(s->preview_win->frame.h, 30);

    send_message(doc->canvas_win, evMouseMove,
                 MAKEDWORD(CANVAS_PADDING + 120, CANVAS_PADDING + 60), NULL);
    send_message(doc->canvas_win, evMouseMove,
                 MAKEDWORD(CANVAS_PADDING + 140, CANVAS_PADDING + 70), NULL);

    ASSERT_TRUE(s->preview_win == first_preview);
    ASSERT_EQUAL(fe_count_canvas_children_with_proc(doc, win_button), 1);
    ASSERT_EQUAL(s->preview_win->frame.w, 120);
    ASSERT_EQUAL(s->preview_win->frame.h, 50);

    send_message(doc->canvas_win, evLeftButtonUp,
                 MAKEDWORD(CANVAS_PADDING + 140, CANVAS_PADDING + 70), NULL);

    fe_teardown();
    PASS();
}

// Each supported control type can be placed and carries the right type enum.
void test_fe_place_all_types(void) {
    TEST("place all control types: element_count=6, types correct");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    static const struct { int tool; int type; } kCases[] = {
        { ID_TOOL_BUTTON,   CTRL_BUTTON   },
        { ID_TOOL_CHECKBOX, CTRL_CHECKBOX },
        { ID_TOOL_LABEL,    CTRL_LABEL    },
        { ID_TOOL_TEXTEDIT, CTRL_TEXTEDIT },
        { ID_TOOL_LIST,     CTRL_LIST     },
        { ID_TOOL_COMBOBOX, CTRL_COMBOBOX },
    };
    int n = (int)(sizeof(kCases) / sizeof(kCases[0]));

    for (int i = 0; i < n; i++)
        fe_place_ctrl(doc, kCases[i].tool, 10 + i * 20, 10, 15, 12);

    ASSERT_EQUAL(doc->element_count, n);
    for (int i = 0; i < n; i++)
        ASSERT_EQUAL(doc->elements[i].type, kCases[i].type);

    fe_teardown();
    PASS();
}

// Placing a control creates a live_win child and an overlay on the canvas.
void test_fe_live_windows_created(void) {
    TEST("place button: live_win created and is_window");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    fe_place_ctrl(doc, ID_TOOL_BUTTON, 10, 10, 60, 20);

    ASSERT_EQUAL(doc->element_count, 1);
    ASSERT_NOT_NULL(doc->elements[0].live_win);          // child window
    ASSERT_TRUE(doc->elements[0].live_win->parent == doc->canvas_win);
    // The overlay must also exist on the canvas.
    canvas_state_t *s = fe_state(doc);
    ASSERT_NOT_NULL(s->overlay_win);                     // child window
    ASSERT_TRUE(s->overlay_win->parent == doc->canvas_win);

    fe_teardown();
    PASS();
}

// Clicking on a placed element sets selected_idx to that element's index.
void test_fe_select_element(void) {
    TEST("select element: selected_idx set by click");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    fe_place_ctrl(doc, ID_TOOL_BUTTON, 30, 30, 80, 24);

    // After placement tool reverts to SELECT; use fe_select to do the click.
    fe_select(doc, 0);

    ASSERT_EQUAL(fe_state(doc)->selected_idx, 0);

    fe_teardown();
    PASS();
}

// If the live design-time control receives the click first, it must forward
// the translated coordinates to the canvas so selection still works.
void test_fe_live_button_forwards_click_to_canvas_selection(void) {
    TEST("live button design wrapper: forwards click for canvas selection");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    fe_place_ctrl(doc, ID_TOOL_BUTTON, 30, 30, 80, 24);
    fe_state(doc)->selected_idx = -1;

    send_message(doc->elements[0].live_win, evLeftButtonDown, MAKEDWORD(4, 4), NULL);
    send_message(doc->elements[0].live_win, evLeftButtonUp,   MAKEDWORD(4, 4), NULL);

    ASSERT_EQUAL(fe_state(doc)->selected_idx, 0);

    fe_teardown();
    PASS();
}

// Clicking an empty area of the canvas clears the selection.
void test_fe_deselect_on_empty_click(void) {
    TEST("click empty area: selected_idx becomes -1");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    fe_place_ctrl(doc, ID_TOOL_BUTTON, 30, 30, 80, 24);
    fe_select(doc, 0);
    ASSERT_EQUAL(fe_state(doc)->selected_idx, 0);

    // Click at (1, 1) — canvas-local, well outside the button.
    g_app->current_tool = ID_TOOL_SELECT;
    send_message(doc->canvas_win, evLeftButtonDown, MAKEDWORD(1, 1), NULL);
    send_message(doc->canvas_win, evLeftButtonUp,   MAKEDWORD(1, 1), NULL);

    ASSERT_EQUAL(fe_state(doc)->selected_idx, -1);

    fe_teardown();
    PASS();
}

// Dragging the BR resize handle changes width and height by the drag delta.
void test_fe_resize_element(void) {
    TEST("resize via BR handle: width and height updated by drag delta");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    fe_place_ctrl(doc, ID_TOOL_BUTTON, 20, 20, 80, 30);
    fe_select(doc, 0);

    int orig_w = doc->elements[0].w;
    int orig_h = doc->elements[0].h;
    ASSERT_EQUAL(orig_w, 80);
    ASSERT_EQUAL(orig_h, 30);

    fe_resize_br(doc, 40, 20);

    ASSERT_EQUAL(doc->elements[0].w, 120);
    ASSERT_EQUAL(doc->elements[0].h, 50);
    ASSERT_EQUAL(doc->elements[0].live_win->frame.w, 120);
    ASSERT_EQUAL(doc->elements[0].live_win->frame.h, 50);
    // Position must not change for a BR drag.
    ASSERT_EQUAL(doc->elements[0].x, 20);
    ASSERT_EQUAL(doc->elements[0].y, 20);
    ASSERT_TRUE(doc->modified);

    fe_teardown();
    PASS();
}

// Resize is clamped to MIN_ELEM_W × MIN_ELEM_H; it never goes below that.
void test_fe_resize_clamped_to_minimum(void) {
    TEST("resize to below minimum: clamped to MIN_ELEM_W x MIN_ELEM_H");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    fe_place_ctrl(doc, ID_TOOL_BUTTON, 20, 20, 80, 30);
    fe_select(doc, 0);

    // Drag BR far to the upper-left — would produce negative size.
    fe_resize_br(doc, -200, -200);

    // MIN_ELEM_W=10, MIN_ELEM_H=8 (defined privately in win_canvas.c)
    ASSERT_TRUE(doc->elements[0].w >= 10);
    ASSERT_TRUE(doc->elements[0].h >= 8);

    fe_teardown();
    PASS();
}

// handle_menu_command(ID_EDIT_DELETE) removes the selected element.
void test_fe_delete_element(void) {
    TEST("delete selected element: element_count decreases to 0");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    fe_place_ctrl(doc, ID_TOOL_BUTTON, 20, 20, 80, 30);
    ASSERT_EQUAL(doc->element_count, 1);

    fe_select(doc, 0);
    handle_menu_command(ID_EDIT_DELETE);

    ASSERT_EQUAL(doc->element_count, 0);
    ASSERT_EQUAL(fe_state(doc)->selected_idx, -1);
    ASSERT_TRUE(doc->modified);

    fe_teardown();
    PASS();
}

// Deleting with no selection is a no-op.
void test_fe_delete_with_no_selection(void) {
    TEST("delete with no selection: element_count unchanged");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    fe_place_ctrl(doc, ID_TOOL_BUTTON, 20, 20, 80, 30);
    ASSERT_EQUAL(doc->element_count, 1);
    g_app->current_tool = ID_TOOL_SELECT;
    send_message(doc->canvas_win, evLeftButtonDown, MAKEDWORD(1, 1), NULL);
    send_message(doc->canvas_win, evLeftButtonUp,   MAKEDWORD(1, 1), NULL);
    ASSERT_EQUAL(fe_state(doc)->selected_idx, -1);

    handle_menu_command(ID_EDIT_DELETE);

    ASSERT_EQUAL(doc->element_count, 1);  // unchanged

    fe_teardown();
    PASS();
}

// Deleting the middle of three elements shifts the array and preserves the
// IDs and types of the remaining two elements.
void test_fe_delete_middle_element(void) {
    TEST("delete middle element: remaining elements shift and preserve identity");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    fe_place_ctrl(doc, ID_TOOL_BUTTON,   10, 10, 60, 20);
    fe_place_ctrl(doc, ID_TOOL_CHECKBOX, 10, 40, 60, 16);
    fe_place_ctrl(doc, ID_TOOL_LABEL,    10, 70, 60, 12);
    ASSERT_EQUAL(doc->element_count, 3);

    int id0 = doc->elements[0].id;
    int id2 = doc->elements[2].id;

    fe_select(doc, 1);  // select the checkbox (index 1)
    handle_menu_command(ID_EDIT_DELETE);

    ASSERT_EQUAL(doc->element_count, 2);
    ASSERT_EQUAL(doc->elements[0].id, id0);
    ASSERT_EQUAL(doc->elements[0].type, CTRL_BUTTON);
    ASSERT_EQUAL(doc->elements[1].id, id2);
    ASSERT_EQUAL(doc->elements[1].type, CTRL_LABEL);

    fe_teardown();
    PASS();
}

// IDs are assigned sequentially starting at CTRL_ID_BASE.
void test_fe_element_ids_sequential(void) {
    TEST("element IDs: assigned sequentially from CTRL_ID_BASE");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    fe_place_ctrl(doc, ID_TOOL_BUTTON,   10, 10, 60, 20);
    fe_place_ctrl(doc, ID_TOOL_LABEL,    10, 40, 60, 12);
    fe_place_ctrl(doc, ID_TOOL_TEXTEDIT, 10, 60, 60, 18);

    ASSERT_EQUAL(doc->elements[0].id, CTRL_ID_BASE);
    ASSERT_EQUAL(doc->elements[1].id, CTRL_ID_BASE + 1);
    ASSERT_EQUAL(doc->elements[2].id, CTRL_ID_BASE + 2);

    fe_teardown();
    PASS();
}

// Names are generated with the correct prefix and a per-type counter.
void test_fe_element_names_generated(void) {
    TEST("element names: correct prefix and per-type counter");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    fe_place_ctrl(doc, ID_TOOL_BUTTON, 10, 10, 60, 20);
    fe_place_ctrl(doc, ID_TOOL_BUTTON, 10, 40, 60, 20);
    fe_place_ctrl(doc, ID_TOOL_LABEL,  10, 70, 60, 12);

    ASSERT_STR_EQUAL(doc->elements[0].name, "IDC_BTN1");
    ASSERT_STR_EQUAL(doc->elements[1].name, "IDC_BTN2");
    ASSERT_STR_EQUAL(doc->elements[2].name, "IDC_LBL1");

    fe_teardown();
    PASS();
}

// form_save + form_load round-trips all element fields correctly.
void test_fe_save_load_roundtrip(void) {
    TEST("save/load roundtrip: element count, type, geometry, text, name preserved");

    // Build the path in a temp directory.
    char path[512];
    snprintf(path, sizeof(path), "%s/orion_fe_test_%d.h",
             fe_temp_dir(), (int)getpid());

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    fe_place_ctrl(doc, ID_TOOL_BUTTON,   20, 20, 80, 24);
    fe_place_ctrl(doc, ID_TOOL_TEXTEDIT, 20, 56, 120, 18);
    fe_place_ctrl(doc, ID_TOOL_CHECKBOX, 20, 82, 90, 16);

    ASSERT_EQUAL(doc->element_count, 3);

    // Snapshot the original element data for comparison after reload.
    form_element_t orig[3];
    memcpy(orig, doc->elements, 3 * sizeof(form_element_t));

    bool saved = form_save(doc, path);
    ASSERT_TRUE(saved);

    // Create a fresh doc and load the file into it.
    form_doc_t *ndoc = create_form_doc(FORM_DEFAULT_W, FORM_DEFAULT_H);
    ASSERT_NOT_NULL(ndoc);

    bool loaded = form_load(ndoc, path);
    ASSERT_TRUE(loaded);

    ASSERT_EQUAL(ndoc->element_count, 3);
    for (int i = 0; i < 3; i++) {
        ASSERT_EQUAL(ndoc->elements[i].type, orig[i].type);
        ASSERT_EQUAL(ndoc->elements[i].x,    orig[i].x);
        ASSERT_EQUAL(ndoc->elements[i].y,    orig[i].y);
        ASSERT_EQUAL(ndoc->elements[i].w,    orig[i].w);
        ASSERT_EQUAL(ndoc->elements[i].h,    orig[i].h);
        ASSERT_STR_EQUAL(ndoc->elements[i].text, orig[i].text);
        ASSERT_STR_EQUAL(ndoc->elements[i].name, orig[i].name);
    }

    // Clean up the temp file.
    unlink(path);

    fe_teardown();
    PASS();
}

// form_load preserves form dimensions stored in the .h file.
void test_fe_save_load_form_dimensions(void) {
    TEST("save/load: form_w and form_h round-trip correctly");

    char path[512];
    snprintf(path, sizeof(path), "%s/orion_fe_dims_%d.h",
             fe_temp_dir(), (int)getpid());

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->form_w = 400;
    doc->form_h = 300;

    bool saved = form_save(doc, path);
    ASSERT_TRUE(saved);

    form_doc_t *ndoc = create_form_doc(FORM_DEFAULT_W, FORM_DEFAULT_H);
    ASSERT_NOT_NULL(ndoc);
    bool loaded = form_load(ndoc, path);
    ASSERT_TRUE(loaded);

    ASSERT_EQUAL(ndoc->form_w, 400);
    ASSERT_EQUAL(ndoc->form_h, 300);

    unlink(path);
    fe_teardown();
    PASS();
}

// ID_FILE_NEW replaces the current document with an empty one.
void test_fe_file_new_resets_doc(void) {
    TEST("ID_FILE_NEW: replaces doc with empty form");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    fe_place_ctrl(doc, ID_TOOL_BUTTON, 10, 10, 60, 20);
    ASSERT_EQUAL(doc->element_count, 1);

    handle_menu_command(ID_FILE_NEW);

    // g_app->doc must be a fresh, empty document now.
    ASSERT_NOT_NULL(g_app->doc);
    ASSERT_EQUAL(g_app->doc->element_count, 0);
    ASSERT_FALSE(g_app->doc->modified);

    fe_teardown();
    PASS();
}

// ── main ──────────────────────────────────────────────────────────────────

int main(void) {
    TEST_START("Form Editor CRUD");

    test_fe_create_doc();
    test_fe_close_doc();
    test_fe_place_button();
    test_fe_button_preview_visible_while_dragging();
    test_fe_place_all_types();
    test_fe_live_windows_created();
    test_fe_select_element();
    test_fe_live_button_forwards_click_to_canvas_selection();
    test_fe_deselect_on_empty_click();
    test_fe_resize_element();
    test_fe_resize_clamped_to_minimum();
    test_fe_delete_element();
    test_fe_delete_with_no_selection();
    test_fe_delete_middle_element();
    test_fe_element_ids_sequential();
    test_fe_element_names_generated();
    test_fe_save_load_roundtrip();
    test_fe_save_load_form_dimensions();
    test_fe_file_new_resets_doc();

    TEST_END();
}
