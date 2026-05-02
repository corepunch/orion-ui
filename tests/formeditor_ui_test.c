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
#include "../commctl/commctl.h"
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

static void fe_close_all_docs(void) {
    if (!g_app) return;
    while (g_app->docs)
        close_form_doc(g_app->docs);
}

static void fe_setup(void) {
    // If a previous test exited via FAIL() without reaching fe_teardown(), the
    // global g_app and its document window are still alive.  Clean them up
    // explicitly so test_env_init() finds a consistent state.
    if (g_app) {
        fe_close_all_docs();
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
    // close_form_doc destroys each document window tree and frees its struct.
    fe_close_all_docs();
    if (g_app->prop_win)
        destroy_window(g_app->prop_win);
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
    int sx0 = fx;
    int sy0 = fy;
    int sx1 = fx + fw;
    int sy1 = fy + fh;
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
    int sx0 = fx;
    int sy0 = fy;
    int sx1 = fx + fw;
    int sy1 = fy + fh;
    g_app->current_tool = tool;
    send_message(cwin, evLeftButtonDown, MAKEDWORD(sx0, sy0), NULL);
    send_message(cwin, evMouseMove,      MAKEDWORD(sx1, sy1), NULL);
}

// Click on an element to select it (DOWN then UP with ID_TOOL_SELECT).
static void fe_select(form_doc_t *doc, int elem_idx) {
    form_element_t *el = &doc->elements[elem_idx];
    window_t *cwin = doc->canvas_win;
    // Click one pixel inside the element's top-left corner.
    int sx = el->frame.x + 1;
    int sy = el->frame.y + 1;
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
    // BR handle top-left: (el->frame.x + el->frame.w - HANDLE_HALF,
    //                       el->frame.y + el->frame.h - HANDLE_HALF)
    // HANDLE_HALF = HANDLE_SIZE/2 = 5/2 = 2
    int hx = el->frame.x + el->frame.w - 2;
    int hy = el->frame.y + el->frame.h - 2;
    send_message(cwin, evLeftButtonDown, MAKEDWORD(hx,      hy),      NULL);
    send_message(cwin, evMouseMove,      MAKEDWORD(hx + dw, hy + dh), NULL);
    send_message(cwin, evLeftButtonUp,   MAKEDWORD(hx + dw, hy + dh), NULL);
}

// Simulate a BR resize drag starting just outside the visible 5x5 handle.
// The canvas keeps the drawn handle small but accepts a larger hit target.
static void fe_resize_br_from_hit_margin(form_doc_t *doc, int dw, int dh) {
    form_element_t *el = &doc->elements[
        ((canvas_state_t *)doc->canvas_win->userdata)->selected_idx];
    window_t *cwin = doc->canvas_win;
    int hx = el->frame.x + el->frame.w - 2;
    int hy = el->frame.y + el->frame.h - 2;
    int sx = hx + 6;
    int sy = hy + 6;
    send_message(cwin, evLeftButtonDown, MAKEDWORD(sx,      sy),      NULL);
    send_message(cwin, evMouseMove,      MAKEDWORD(sx + dw, sy + dh), NULL);
    send_message(cwin, evLeftButtonUp,   MAKEDWORD(sx + dw, sy + dh), NULL);
}

// Return a pointer to the canvas state for the given document.
static canvas_state_t *fe_state(form_doc_t *doc) {
    return (canvas_state_t *)doc->canvas_win->userdata;
}

static window_t *fe_create_property_browser(void) {
    g_app->prop_win = property_browser_create(0);
    return g_app->prop_win ? g_app->prop_win->children : NULL;
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
    ASSERT_EQUAL(doc->form_size.w, FORM_DEFAULT_W);
    ASSERT_EQUAL(doc->form_size.h, FORM_DEFAULT_H);
    ASSERT_EQUAL(doc->flags, 0);
    ASSERT_FALSE((doc->doc_win->flags & WINDOW_STATUSBAR) != 0);
    ASSERT_FALSE(doc->modified);
    ASSERT_EQUAL(doc->next_id, CTRL_ID_BASE);

    fe_teardown();
    PASS();
}

// The document window should hug the form when it fits, so the canvas does
// not show workspace outside the form surface.
void test_fe_create_doc_sizes_canvas_to_form(void) {
    TEST("create_form_doc: canvas is exact form size when it fits");

    fe_setup();
    form_doc_t *doc = g_app->doc;

    ASSERT_EQUAL(doc->doc_win->frame.w, FORM_DEFAULT_W);
    ASSERT_EQUAL(doc->doc_win->frame.h, TITLEBAR_HEIGHT + FORM_DEFAULT_H);
    ASSERT_EQUAL(doc->canvas_win->frame.w, FORM_DEFAULT_W);
    ASSERT_EQUAL(doc->canvas_win->frame.h, FORM_DEFAULT_H);
    ASSERT_FALSE(doc->doc_win->hscroll.visible);
    ASSERT_FALSE(doc->canvas_win->vscroll.visible);

    fe_teardown();
    PASS();
}

// Oversized forms clamp the document window to the desktop and expose only the
// needed scrollbars.
void test_fe_create_large_doc_adds_needed_scrollbars(void) {
    TEST("create_form_doc: large forms fit desktop and scroll");

    fe_setup();
    form_doc_t *doc = create_form_doc(1000, 1000);
    int max_w = SCREEN_W - 4;
    int max_h = SCREEN_H - MENUBAR_HEIGHT - 4;

    ASSERT_NOT_NULL(doc);
    ASSERT_EQUAL(doc->doc_win->frame.w, max_w);
    ASSERT_EQUAL(doc->doc_win->frame.h, max_h);
    ASSERT_EQUAL(doc->canvas_win->frame.w, max_w);
    ASSERT_EQUAL(doc->canvas_win->frame.h,
                 max_h - TITLEBAR_HEIGHT - SCROLLBAR_WIDTH);
    ASSERT_TRUE(doc->doc_win->hscroll.visible);
    ASSERT_TRUE(doc->canvas_win->vscroll.visible);

    fe_teardown();
    PASS();
}

// Resizing the document window changes the designed form size itself.  The
// form editor is not a scrollable image canvas; the window client is the form.
void test_fe_doc_resize_updates_form_size(void) {
    TEST("document resize: updates form_size and canvas size");

    fe_setup();
    form_doc_t *doc = g_app->doc;

    int new_w = 380;
    int new_h = 260;
    resize_window(doc->doc_win, new_w, TITLEBAR_HEIGHT + new_h);

    ASSERT_EQUAL(doc->form_size.w, new_w);
    ASSERT_EQUAL(doc->form_size.h, new_h);
    ASSERT_EQUAL(doc->canvas_win->frame.w, new_w);
    ASSERT_EQUAL(doc->canvas_win->frame.h, new_h);
    ASSERT_TRUE(doc->modified);

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

// Creating another document adds a new window and keeps the existing document
// alive; opening/new must not discard work without explicit user consent.
void test_fe_create_doc_keeps_existing_doc(void) {
    TEST("create_form_doc: adds document without closing existing one");

    fe_setup();
    form_doc_t *first = g_app->doc;
    window_t *first_win = first->doc_win;
    first->snap_to_grid = false;
    fe_place_ctrl(first, ID_TOOL_BUTTON, 10, 10, 60, 20);

    form_doc_t *second = create_form_doc(FORM_DEFAULT_W, FORM_DEFAULT_H);

    ASSERT_NOT_NULL(second);
    ASSERT_TRUE(g_app->doc == second);
    ASSERT_TRUE(is_window(first_win));
    ASSERT_EQUAL(first->element_count, 1);
    ASSERT_EQUAL(second->element_count, 0);

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
    ASSERT_EQUAL(doc->elements[0].frame.x, 20);
    ASSERT_EQUAL(doc->elements[0].frame.y, 20);
    ASSERT_EQUAL(doc->elements[0].frame.w, 80);
    ASSERT_EQUAL(doc->elements[0].frame.h, 30);
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
    ASSERT_EQUAL(s->preview_win->frame.x, 20);
    ASSERT_EQUAL(s->preview_win->frame.y, 20);
    ASSERT_EQUAL(s->preview_win->frame.w, 80);
    ASSERT_EQUAL(s->preview_win->frame.h, 30);

    send_message(doc->canvas_win, evMouseMove,
                 MAKEDWORD(120, 60), NULL);
    send_message(doc->canvas_win, evMouseMove,
                 MAKEDWORD(140, 70), NULL);

    ASSERT_TRUE(s->preview_win == first_preview);
    ASSERT_EQUAL(s->preview_win->frame.w, 120);
    ASSERT_EQUAL(s->preview_win->frame.h, 50);

    send_message(doc->canvas_win, evLeftButtonUp,
                 MAKEDWORD(140, 70), NULL);

    fe_teardown();
    PASS();
}

// Starting a new placement clears the old selection.  The rubber-band for the
// control being drawn is the only outline shown during placement.
void test_fe_begin_place_drag_deselects_previous_element(void) {
    TEST("place drag: starting new placement clears selection");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    fe_place_ctrl(doc, ID_TOOL_BUTTON, 20, 20, 80, 30);
    ASSERT_EQUAL(fe_state(doc)->selected_idx, 0);

    fe_begin_place_drag(doc, ID_TOOL_LABEL, 40, 50, 60, 16);

    ASSERT_EQUAL(fe_state(doc)->selected_idx, -1);

    send_message(doc->canvas_win, evLeftButtonUp, MAKEDWORD(100, 66), NULL);

    fe_teardown();
    PASS();
}

// If a live preview receives mouse-up directly, parent notification lets the
// canvas finalize placement before the preview control can handle the event.
void test_fe_preview_parent_notify_finishes_placement(void) {
    TEST("place button drag: preview parent notify finalizes placement");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    fe_begin_place_drag(doc, ID_TOOL_BUTTON, 20, 20, 80, 30);

    canvas_state_t *s = fe_state(doc);
    ASSERT_NOT_NULL(s->preview_win);
    send_message(s->preview_win, evLeftButtonUp, MAKEDWORD(80, 30), NULL);

    ASSERT_EQUAL(doc->element_count, 1);
    ASSERT_EQUAL(doc->elements[0].frame.x, 20);
    ASSERT_EQUAL(doc->elements[0].frame.y, 20);
    ASSERT_EQUAL(doc->elements[0].frame.w, 80);
    ASSERT_EQUAL(doc->elements[0].frame.h, 30);
    ASSERT_EQUAL(s->drag.mode, DRAG_NONE);
    ASSERT_NULL(g_ui_runtime.captured);
    ASSERT_EQUAL(g_app->current_tool, ID_TOOL_SELECT);

    fe_teardown();
    PASS();
}

// Placement remembers the control type selected at mouse-down.  A toolbar
// state change before mouse-up must not strand the drag or prevent commit.
void test_fe_placement_type_latched_on_mousedown(void) {
    TEST("place button drag: control type is latched at mouse-down");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    fe_begin_place_drag(doc, ID_TOOL_BUTTON, 20, 20, 80, 30);
    g_app->current_tool = ID_TOOL_SELECT;
    send_message(doc->canvas_win, evLeftButtonUp, MAKEDWORD(100, 50), NULL);

    ASSERT_EQUAL(doc->element_count, 1);
    ASSERT_EQUAL(doc->elements[0].type, CTRL_BUTTON);
    ASSERT_EQUAL(doc->elements[0].frame.x, 20);
    ASSERT_EQUAL(doc->elements[0].frame.y, 20);
    ASSERT_EQUAL(doc->elements[0].frame.w, 80);
    ASSERT_EQUAL(doc->elements[0].frame.h, 30);
    ASSERT_EQUAL(fe_state(doc)->drag.mode, DRAG_NONE);
    ASSERT_NULL(g_ui_runtime.captured);

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

// Placing a control creates a live_win child on the canvas.
void test_fe_live_windows_created(void) {
    TEST("place button: live_win created and is_window");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    fe_place_ctrl(doc, ID_TOOL_BUTTON, 10, 10, 60, 20);

    ASSERT_EQUAL(doc->element_count, 1);
    ASSERT_NOT_NULL(doc->elements[0].live_win);          // child window
    ASSERT_TRUE(doc->elements[0].live_win->parent == doc->canvas_win);

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

// If the live design-time control receives the click first, parent notification
// lets the canvas select it before the control can handle the event.
void test_fe_live_button_parent_notify_selects_on_click(void) {
    TEST("live button parent notify selects on click");

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

    int orig_w = doc->elements[0].frame.w;
    int orig_h = doc->elements[0].frame.h;
    ASSERT_EQUAL(orig_w, 80);
    ASSERT_EQUAL(orig_h, 30);

    fe_resize_br(doc, 40, 20);

    ASSERT_EQUAL(doc->elements[0].frame.w, 120);
    ASSERT_EQUAL(doc->elements[0].frame.h, 50);
    ASSERT_EQUAL(doc->elements[0].live_win->frame.w, 120);
    ASSERT_EQUAL(doc->elements[0].live_win->frame.h, 50);
    // Position must not change for a BR drag.
    ASSERT_EQUAL(doc->elements[0].frame.x, 20);
    ASSERT_EQUAL(doc->elements[0].frame.y, 20);
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
    ASSERT_TRUE(doc->elements[0].frame.w >= 10);
    ASSERT_TRUE(doc->elements[0].frame.h >= 8);

    fe_teardown();
    PASS();
}

// Resize handles draw as small squares, but the mouse hit target is larger
// so users do not have to land exactly on a 5x5 pixel handle.
void test_fe_resize_handle_has_larger_hit_area(void) {
    TEST("resize handle: hit target extends beyond visible square");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;

    fe_place_ctrl(doc, ID_TOOL_BUTTON, 20, 20, 80, 30);
    fe_select(doc, 0);

    fe_resize_br_from_hit_margin(doc, 12, 8);

    ASSERT_EQUAL(doc->elements[0].frame.w, 92);
    ASSERT_EQUAL(doc->elements[0].frame.h, 38);

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

// The property browser is a reportview-backed two-column list, refreshed from
// the current canvas selection.
void test_fe_property_browser_uses_reportview_for_selection(void) {
    TEST("property browser: reportview shows selected element basics");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;
    window_t *list = fe_create_property_browser();
    ASSERT_NOT_NULL(g_app->prop_win);
    ASSERT_NOT_NULL(list);
    ASSERT_FALSE(send_message(list, RVM_GETCOLUMNTITLESVISIBLE, 0, NULL));

    fe_place_ctrl(doc, ID_TOOL_BUTTON, 20, 30, 80, 24);
    fe_select(doc, 0);

    int prop_count = (int)send_message(list, RVM_GETITEMCOUNT, 0, NULL);
    ASSERT_EQUAL(prop_count, 8);

    reportview_item_t item = {0};
    ASSERT_TRUE(send_message(list, RVM_GETITEMDATA, 0, &item));
    ASSERT_STR_EQUAL(item.text, "(Name)");
    ASSERT_STR_EQUAL(item.subitems[0], "IDC_BTN1");

    ASSERT_TRUE(send_message(list, RVM_GETITEMDATA, 1, &item));
    ASSERT_STR_EQUAL(item.text, "Caption");
    ASSERT_STR_EQUAL(item.subitems[0], "Button1");

    ASSERT_TRUE(send_message(list, RVM_GETITEMDATA, 4, &item));
    ASSERT_STR_EQUAL(item.text, "Left");
    ASSERT_STR_EQUAL(item.subitems[0], "20");

    fe_teardown();
    PASS();
}

// Clicking a value cell creates an in-place text edit over that report
// cell; Enter commits back into the selected element and refreshes the grid.
void test_fe_property_browser_edits_caption_in_place(void) {
    TEST("property browser: in-place edit commits caption");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;
    window_t *list = fe_create_property_browser();
    ASSERT_NOT_NULL(g_app->prop_win);
    ASSERT_NOT_NULL(list);

    fe_place_ctrl(doc, ID_TOOL_BUTTON, 20, 30, 80, 24);
    fe_select(doc, 0);

    int y = 1 * COLUMNVIEW_ENTRY_HEIGHT + 1;
    send_message(list, evLeftButtonDown, MAKEDWORD(80, y), NULL);

    window_t *edit = list->children;
    ASSERT_NOT_NULL(edit);
    ASSERT_STR_EQUAL(edit->title, "Button1");

    for (int i = 0; i < 7; i++)
        send_message(edit, evKeyDown, AX_KEY_BACKSPACE, NULL);
    const char *new_caption = "OK";
    for (const char *p = new_caption; *p; p++) {
        char ch = *p;
        send_message(edit, evTextInput, 0, &ch);
    }
    send_message(edit, evKeyDown, AX_KEY_ENTER, NULL);

    ASSERT_STR_EQUAL(doc->elements[0].text, "OK");
    ASSERT_STR_EQUAL(doc->elements[0].live_win->title, "OK");
    ASSERT_NULL(list->children);

    reportview_item_t item = {0};
    ASSERT_TRUE(send_message(list, RVM_GETITEMDATA, 1, &item));
    ASSERT_STR_EQUAL(item.text, "Caption");
    ASSERT_STR_EQUAL(item.subitems[0], "OK");

    fe_teardown();
    PASS();
}

// When the property reportview has a vertical scrollbar, the in-place editor
// must stay inside the value column and not cover the scrollbar strip.
void test_fe_property_browser_edit_respects_vertical_scrollbar(void) {
    TEST("property browser: in-place edit avoids vertical scrollbar");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;
    window_t *list = fe_create_property_browser();
    ASSERT_NOT_NULL(g_app->prop_win);
    ASSERT_NOT_NULL(list);

    fe_place_ctrl(doc, ID_TOOL_BUTTON, 20, 30, 80, 24);
    fe_select(doc, 0);

    show_scroll_bar(list, SB_VERT, true);
    int y = 1 * COLUMNVIEW_ENTRY_HEIGHT + 1;
    send_message(list, evLeftButtonDown, MAKEDWORD(80, y), NULL);

    window_t *edit = list->children;
    ASSERT_NOT_NULL(edit);
    ASSERT_EQUAL(edit->frame.x, 72);
    ASSERT_EQUAL(edit->frame.w, list->frame.w - 72 - SCROLLBAR_WIDTH);
    ASSERT_TRUE(edit->frame.x + edit->frame.w <= list->frame.w - SCROLLBAR_WIDTH);

    send_message(edit, evKeyDown, AX_KEY_ESCAPE, NULL);

    fe_teardown();
    PASS();
}

// form_project_save + form_project_load round-trips all element fields correctly.
void test_fe_save_load_roundtrip(void) {
    TEST("project save/load roundtrip: element count, type, geometry, text, name preserved");

    // Build the path in a temp directory.
    char path[512];
    snprintf(path, sizeof(path), "%s/orion_fe_test_%d.orion",
             fe_temp_dir(), (int)getpid());

    fe_setup();
    form_doc_t *doc = g_app->doc;
    doc->snap_to_grid = false;
    snprintf(doc->form_id, sizeof(doc->form_id), "%s", "roundtrip");
    snprintf(doc->form_title, sizeof(doc->form_title), "%s", "Roundtrip");

    fe_place_ctrl(doc, ID_TOOL_BUTTON,   20, 20, 80, 24);
    fe_place_ctrl(doc, ID_TOOL_TEXTEDIT, 20, 56, 120, 18);
    fe_place_ctrl(doc, ID_TOOL_CHECKBOX, 20, 82, 90, 16);

    ASSERT_EQUAL(doc->element_count, 3);

    // Snapshot the original element data for comparison after reload.
    form_element_t orig[3];
    memcpy(orig, doc->elements, 3 * sizeof(form_element_t));

    bool saved = form_project_save(path);
    ASSERT_TRUE(saved);

    bool loaded = form_project_load(path);
    ASSERT_TRUE(loaded);

    form_doc_t *ndoc = g_app->docs;
    ASSERT_NOT_NULL(ndoc);
    ASSERT_STR_EQUAL(ndoc->form_id, "roundtrip");
    ASSERT_STR_EQUAL(ndoc->form_title, "Roundtrip");
    ASSERT_EQUAL(ndoc->element_count, 3);
    for (int i = 0; i < 3; i++) {
        ASSERT_EQUAL(ndoc->elements[i].type, orig[i].type);
        ASSERT_EQUAL(ndoc->elements[i].frame.x,    orig[i].frame.x);
        ASSERT_EQUAL(ndoc->elements[i].frame.y,    orig[i].frame.y);
        ASSERT_EQUAL(ndoc->elements[i].frame.w,    orig[i].frame.w);
        ASSERT_EQUAL(ndoc->elements[i].frame.h,    orig[i].frame.h);
        ASSERT_STR_EQUAL(ndoc->elements[i].text, orig[i].text);
        ASSERT_STR_EQUAL(ndoc->elements[i].name, orig[i].name);
    }

    // Clean up the temp file.
    unlink(path);

    fe_teardown();
    PASS();
}

// form_project_load preserves form dimensions stored in the .orion file.
void test_fe_save_load_form_dimensions(void) {
    TEST("project save/load: form_size round-trips correctly");

    char path[512];
    snprintf(path, sizeof(path), "%s/orion_fe_dims_%d.orion",
             fe_temp_dir(), (int)getpid());

    fe_setup();
    form_doc_t *doc = g_app->doc;
    snprintf(doc->form_id, sizeof(doc->form_id), "%s", "dimensions");
    doc->form_size.w = 400;
    doc->form_size.h = 300;

    bool saved = form_project_save(path);
    ASSERT_TRUE(saved);

    bool loaded = form_project_load(path);
    ASSERT_TRUE(loaded);

    form_doc_t *ndoc = g_app->docs;
    ASSERT_NOT_NULL(ndoc);
    ASSERT_EQUAL(ndoc->form_size.w, 400);
    ASSERT_EQUAL(ndoc->form_size.h, 300);

    unlink(path);
    fe_teardown();
    PASS();
}

// form_project_save + form_project_load preserve form/window flags such as WINDOW_STATUSBAR.
void test_fe_save_load_form_flags(void) {
    TEST("project save/load: form flags round-trip correctly");

    char path[512];
    snprintf(path, sizeof(path), "%s/orion_fe_flags_%d.orion",
             fe_temp_dir(), (int)getpid());

    fe_setup();
    form_doc_t *doc = g_app->doc;
    snprintf(doc->form_id, sizeof(doc->form_id), "%s", "flags");
    doc->flags = WINDOW_STATUSBAR;

    bool saved = form_project_save(path);
    ASSERT_TRUE(saved);

    bool loaded = form_project_load(path);
    ASSERT_TRUE(loaded);

    form_doc_t *ndoc = g_app->docs;
    ASSERT_NOT_NULL(ndoc);
    ASSERT_EQUAL(ndoc->flags, WINDOW_STATUSBAR);

    unlink(path);
    fe_teardown();
    PASS();
}

// ID_FILE_NEW adds an empty document without closing the current one.
void test_fe_file_new_adds_doc_without_dropping_current(void) {
    TEST("ID_FILE_NEW: adds empty form and keeps current document");

    fe_setup();
    form_doc_t *doc = g_app->doc;
    window_t *doc_win = doc->doc_win;
    doc->snap_to_grid = false;

    fe_place_ctrl(doc, ID_TOOL_BUTTON, 10, 10, 60, 20);
    ASSERT_EQUAL(doc->element_count, 1);

    handle_menu_command(ID_FILE_NEW);

    ASSERT_NOT_NULL(g_app->doc);
    ASSERT_TRUE(g_app->doc != doc);
    ASSERT_EQUAL(g_app->doc->element_count, 0);
    ASSERT_FALSE(g_app->doc->modified);
    ASSERT_TRUE(is_window(doc_win));
    ASSERT_EQUAL(doc->element_count, 1);

    fe_teardown();
    PASS();
}

// ── main ──────────────────────────────────────────────────────────────────

int main(void) {
    TEST_START("Form Editor CRUD");

    test_fe_create_doc();
    test_fe_create_doc_sizes_canvas_to_form();
    test_fe_create_large_doc_adds_needed_scrollbars();
    test_fe_doc_resize_updates_form_size();
    test_fe_close_doc();
    test_fe_create_doc_keeps_existing_doc();
    test_fe_place_button();
    test_fe_button_preview_visible_while_dragging();
    test_fe_begin_place_drag_deselects_previous_element();
    test_fe_preview_parent_notify_finishes_placement();
    test_fe_placement_type_latched_on_mousedown();
    test_fe_place_all_types();
    test_fe_live_windows_created();
    test_fe_select_element();
    test_fe_live_button_parent_notify_selects_on_click();
    test_fe_deselect_on_empty_click();
    test_fe_resize_element();
    test_fe_resize_clamped_to_minimum();
    test_fe_resize_handle_has_larger_hit_area();
    test_fe_delete_element();
    test_fe_delete_with_no_selection();
    test_fe_delete_middle_element();
    test_fe_element_ids_sequential();
    test_fe_element_names_generated();
    test_fe_property_browser_uses_reportview_for_selection();
    test_fe_property_browser_edits_caption_in_place();
    test_fe_property_browser_edit_respects_vertical_scrollbar();
    test_fe_save_load_roundtrip();
    test_fe_save_load_form_dimensions();
    test_fe_save_load_form_flags();
    test_fe_file_new_adds_doc_without_dropping_current();

    TEST_END();
}
