#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>

static result_t owned_test_proc(window_t *win, uint32_t msg, uint32_t wp, void *lp) {
  (void)win; (void)wp; (void)lp;
  return msg == evCreate || msg == evPaint || msg == evDestroy;
}
static window_t *owned_test_window(const char *title, flags_t flags) {
  return create_window(title, flags, MAKERECT(30, 40, 200, 160), NULL, owned_test_proc, 0, NULL);
}
static int z_index(window_t *target) {
  int i = 0;
  for (window_t *w = g_ui_runtime.windows; w; w = w->next, i++) if (w == target) return i;
  return -1;
}
static void test_ownership(void) {
  TEST("owned popups: screen coordinates, independent visibility intent, automatic destruction");
  window_t *frame = owned_test_window("Frame", 0);
  window_t *control = create_window("Control", 0, MAKERECT(2, 3, 20, 20), frame, owned_test_proc, 0, NULL);
  window_t *popup = owned_test_window("Assistant", WINDOW_TOOLWINDOW | WINDOW_NOACTIVATE);
  ASSERT_TRUE(window_set_owner(popup, control));
  ASSERT(popup->owner == frame && popup->parent == NULL, "ownership is separate from child layout");
  ASSERT_EQUAL(window_screen_x(popup), 30);
  ASSERT_EQUAL(window_screen_y(popup), 40);
  ASSERT_FALSE(window_set_owner(frame, popup));
  show_window(frame, false);
  ASSERT_FALSE(window_is_visible(popup));
  ASSERT(popup->flags & WINDOW_STATE_VISIBLE, "owner suppression preserves the popup's own visibility intent");
  show_window(frame, true);
  ASSERT_TRUE(window_is_visible(popup));
  show_window(popup, false);
  show_window(frame, false); show_window(frame, true);
  ASSERT_FALSE(window_is_visible(popup));
  show_window(popup, true);
  destroy_window(frame);
  ASSERT_FALSE(is_window(popup));
  ASSERT_FALSE(is_window(control));
  PASS();
}
static void test_z_order(void) {
  TEST("owned windows: normal documents, assistant palette and modal dialogs have distinct z bands");
  window_t *frame = owned_test_window("Frame", 0);
  window_t *palette = owned_test_window("Clippy", WINDOW_TOOLWINDOW | WINDOW_ALWAYSONTOP | WINDOW_NOACTIVATE);
  window_t *document = owned_test_window("Document", 0);
  window_t *dialog = owned_test_window("Dialog", WINDOW_DIALOG);
  window_t *other = owned_test_window("Other app", 0);
  ASSERT_TRUE(window_set_owner(palette, frame));
  ASSERT_TRUE(window_set_owner(document, frame));
  ASSERT_TRUE(window_set_owner(dialog, document));
  move_to_top(frame);
  ASSERT(z_index(frame) < z_index(document), "document stays above its frame");
  ASSERT(z_index(document) < z_index(palette), "assistant floats above documents");
  ASSERT(z_index(palette) < z_index(dialog), "modal dialog stays above the assistant");
  move_to_top(other);
  ASSERT(z_index(dialog) < z_index(other), "an inactive application's assistant is not globally topmost");
  move_to_top(palette);
  ASSERT(z_index(other) < z_index(frame), "activating the palette brings its owner group forward");
  ASSERT(z_index(palette) < z_index(dialog), "palette activation cannot overtake a modal dialog");
  destroy_window(frame); destroy_window(other);
  PASS();
}
static void test_modal_scope(void) {
  TEST("owner modality: document and assistant input blocked, other applications and nested dialog allowed");
  window_t *frame = owned_test_window("Frame", 0);
  window_t *palette = owned_test_window("Clippy", WINDOW_TOOLWINDOW);
  window_t *dialog = owned_test_window("Dialog", WINDOW_DIALOG);
  window_t *nested = owned_test_window("Nested", WINDOW_DIALOG);
  window_t *other = owned_test_window("Other app", 0);
  window_set_owner(palette, frame); window_set_owner(dialog, frame); window_set_owner(nested, dialog);
  g_ui_runtime.modal_window = dialog;
  ASSERT_TRUE(window_is_modal_blocked(frame));
  ASSERT_FALSE(window_is_disabled(frame));
  ASSERT_TRUE(window_is_modal_blocked(palette));
  ASSERT_FALSE(window_is_modal_blocked(dialog));
  ASSERT_FALSE(window_is_modal_blocked(other));
  ASSERT_FALSE(window_is_enabled(frame));
  ASSERT_FALSE(window_is_enabled(palette));
  ASSERT_TRUE(window_is_enabled(dialog));
  ASSERT_TRUE(window_is_enabled(other));
  g_ui_runtime.modal_window = nested;
  ASSERT_FALSE(window_is_enabled(dialog));
  ASSERT_TRUE(window_is_enabled(nested));
  ASSERT_TRUE(window_is_modal_blocked(dialog));
  ASSERT_FALSE(window_is_modal_blocked(nested));
  g_ui_runtime.modal_window = dialog;
  ASSERT_TRUE(window_is_enabled(dialog));
  ASSERT_FALSE(window_is_disabled(frame));
  enable_window(palette, false);
  g_ui_runtime.modal_window = NULL;
  ASSERT_TRUE(window_is_enabled(frame));
  ASSERT_FALSE(window_is_enabled(palette));
  ASSERT_TRUE(window_is_disabled(palette));
  g_ui_runtime.modal_window = nested;
  destroy_window(frame);
  ASSERT_NULL(g_ui_runtime.modal_window);
  destroy_window(other);
  PASS();
}
static void test_mdi_documents(void) {
  TEST("MDI: client parent, active documents, cycling, maximize bounds and clipped hit-testing");
  window_t *frame = owned_test_window("MDI Frame", 0);
  window_t *client = create_window("", 0, MAKERECT(0, 0, 200, 100), frame, "MDIClient", 0, NULL);
  ASSERT_NOT_NULL(client);
  window_t *a = owned_test_window("A", WINDOW_MAXIMIZEBOX);
  window_t *b = owned_test_window("B", WINDOW_MAXIMIZEBOX);
  window_t *c = owned_test_window("C", WINDOW_MAXIMIZEBOX);
  ASSERT_TRUE(mdi_attach_document(a, client)); ASSERT_TRUE(mdi_attach_document(b, client)); ASSERT_TRUE(mdi_attach_document(c, client));
  ASSERT(a->mdi_client == client && a->owner == NULL, "document parenting is not popup ownership");
  window_t *dialog = owned_test_window("Document dialog", WINDOW_DIALOG);
  window_set_owner(dialog, a);
  g_ui_runtime.modal_window = dialog;
  ASSERT_FALSE(window_is_enabled(frame));
  ASSERT_FALSE(window_is_enabled(a));
  ASSERT_FALSE(window_is_enabled(b));
  ASSERT_TRUE(window_is_enabled(dialog));
  ASSERT_FALSE(window_is_disabled(a));
  g_ui_runtime.modal_window = NULL;
  destroy_window(dialog);
  ASSERT_FALSE(window_set_owner(a, frame));
  ASSERT(mdi_active_document(client) == c, "newly created document is active");
  mdi_cycle_document(client, -1); window_t *first = mdi_active_document(client);
  mdi_cycle_document(client, -1); window_t *second = mdi_active_document(client);
  ASSERT(first != second && first != c && second != c, "reverse cycling visits every document");
  ASSERT_TRUE(maximize_window(a));
  irect16_t area = mdi_client_screen_rect(client);
  ASSERT_EQUAL(a->frame.x, area.x); ASSERT_EQUAL(a->frame.y, area.y);
  ASSERT_EQUAL(a->frame.w, area.w); ASSERT_EQUAL(a->frame.h, area.h);
  move_window(frame, frame->frame.x + 20, frame->frame.y + 10);
  ASSERT_EQUAL(a->frame.x, area.x + 20); ASSERT_EQUAL(a->frame.y, area.y + 10);
  restore_window(a);
  move_window(a, frame->frame.x - 60, frame->frame.y);
  move_to_top(a);
  window_t *hit = find_window(frame->frame.x - 20, frame->frame.y + 40);
  ASSERT(!hit || get_root_window(hit) != a, "document outside the MDI client is not hittable");
  show_window(frame, false); ASSERT_FALSE(window_is_visible(a));
  show_window(frame, true); ASSERT_TRUE(window_is_visible(a));
  ASSERT_TRUE(mdi_close_all(client));
  ASSERT_FALSE(is_window(a)); ASSERT_FALSE(is_window(b)); ASSERT_FALSE(is_window(c));
  destroy_window(frame);
  PASS();
}
static void test_mdi_placement(void) {
  TEST("MDI: new documents open centered and a dragged caption stays inside the client");
  window_t *frame = create_window("MDI Frame", 0, MAKERECT(30, 40, 600, 400), NULL, owned_test_proc, 0, NULL);
  window_t *client = create_window("", 0, MAKERECT(0, 0, 600, 400), frame, "MDIClient", 0, NULL);
  window_t *a = owned_test_window("A", 0), *b = owned_test_window("B", 0);
  ASSERT_TRUE(mdi_attach_document(a, client)); ASSERT_TRUE(mdi_attach_document(b, client));
  irect16_t area = mdi_client_screen_rect(client);
  ASSERT_EQUAL(a->frame.x, area.x + (area.w - 200) / 2); ASSERT_EQUAL(a->frame.y, area.y + (area.h - 160) / 2);
  ASSERT_EQUAL(b->frame.x, a->frame.x + DEFAULT_WINDOW_CASCADE_X); ASSERT_EQUAL(b->frame.y, a->frame.y + DEFAULT_WINDOW_CASCADE_X);
  ipoint16_t above = mdi_constrain_document_origin(a, (ipoint16_t){area.x - 500, area.y - 50});
  ASSERT_EQUAL(above.x, area.x + MDI_MIN_VISIBLE_CAPTION - 200); ASSERT_EQUAL(above.y, area.y);
  ipoint16_t below = mdi_constrain_document_origin(a, (ipoint16_t){area.x + 2000, area.y + 2000});
  ASSERT_EQUAL(below.x, area.x + area.w - MDI_MIN_VISIBLE_CAPTION); ASSERT_EQUAL(below.y, area.y + area.h - titlebar_height(a));
  ASSERT_TRUE(mdi_close_all(client));
  destroy_window(frame);
  PASS();
}
int main(void) {
  TEST_START("Owned windows"); test_env_init();
  test_ownership(); test_z_order(); test_modal_scope(); test_mdi_documents(); test_mdi_placement();
  test_env_shutdown(); TEST_END();
}
