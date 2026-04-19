// Tests for win_toolbar_button
// Covers: mouse click, keyboard activation (RETURN/SPACE), BUTTON_AUTORADIO
// selection, and btnSetImage sanity (doesn't break click behavior).

#include "test_framework.h"
#include "test_env.h"
#include "../ui.h"
#include "../commctl/commctl.h"

extern void repost_messages(void);

// ---- shared test state -------------------------------------------------- //

static int  g_click_count = 0;
static int  g_last_cmd_id = 0;
static window_t *g_last_sender = NULL;

static result_t cmd_parent_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
    (void)win;
    if (msg == evCreate)  return 1;
    if (msg == evDestroy) return 1;
    if (msg == evCommand &&
        HIWORD(wparam) == kButtonNotificationClicked) {
        g_click_count++;
        g_last_cmd_id  = (int)LOWORD(wparam);
        g_last_sender  = (window_t *)lparam;
    }
    return 0;
}

static void reset_state(void) {
    g_click_count = 0;
    g_last_cmd_id = 0;
    g_last_sender = NULL;
}

static void simulate_click(window_t *btn) {
    int cx = btn->frame.x + btn->frame.w / 2;
    int cy = btn->frame.y + btn->frame.h / 2;
    test_env_post_message(btn, evLeftButtonDown, MAKEDWORD(cx, cy), NULL);
    repost_messages();
    test_env_post_message(btn, evLeftButtonUp, MAKEDWORD(cx, cy), NULL);
    repost_messages();
}

// ---- tests -------------------------------------------------------------- //

void test_toolbar_button_click(void) {
    TEST("win_toolbar_button: left-click fires kButtonNotificationClicked");

    test_env_init();
    test_env_enable_tracking(true);
    test_env_clear_events();
    reset_state();

    window_t *parent = test_env_create_window("Parent", 0, 0, 200, 200,
                                               cmd_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    rect_t fr = {10, 10, 20, 20};
    window_t *btn = create_window("T", WINDOW_NOTITLE | WINDOW_NOFILL,
                                  &fr, parent, win_toolbar_button, 0, NULL);
    ASSERT_NOT_NULL(btn);
    btn->id = 200;

    simulate_click(btn);

    ASSERT_EQUAL(g_click_count, 1);
    ASSERT_EQUAL(g_last_cmd_id, 200);
    ASSERT_EQUAL(g_last_sender, btn);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_toolbar_button_keyboard_return(void) {
    TEST("win_toolbar_button: RETURN key fires kButtonNotificationClicked");

    test_env_init();
    test_env_enable_tracking(true);
    test_env_clear_events();
    reset_state();

    window_t *parent = test_env_create_window("Parent", 0, 0, 200, 200,
                                               cmd_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    rect_t fr = {10, 10, 20, 20};
    window_t *btn = create_window("T", WINDOW_NOTITLE | WINDOW_NOFILL,
                                  &fr, parent, win_toolbar_button, 0, NULL);
    ASSERT_NOT_NULL(btn);
    btn->id = 201;

    test_env_post_message(btn, evKeyDown, AX_KEY_ENTER, NULL);
    repost_messages();
    test_env_post_message(btn, evKeyUp,   AX_KEY_ENTER, NULL);
    repost_messages();

    ASSERT_EQUAL(g_click_count, 1);
    ASSERT_EQUAL(g_last_cmd_id, 201);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_toolbar_button_keyboard_space(void) {
    TEST("win_toolbar_button: SPACE key fires kButtonNotificationClicked");

    test_env_init();
    test_env_enable_tracking(true);
    test_env_clear_events();
    reset_state();

    window_t *parent = test_env_create_window("Parent", 0, 0, 200, 200,
                                               cmd_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    rect_t fr = {10, 10, 20, 20};
    window_t *btn = create_window("T", WINDOW_NOTITLE | WINDOW_NOFILL,
                                  &fr, parent, win_toolbar_button, 0, NULL);
    ASSERT_NOT_NULL(btn);
    btn->id = 202;

    test_env_post_message(btn, evKeyDown, AX_KEY_SPACE, NULL);
    repost_messages();
    test_env_post_message(btn, evKeyUp,   AX_KEY_SPACE, NULL);
    repost_messages();

    ASSERT_EQUAL(g_click_count, 1);
    ASSERT_EQUAL(g_last_cmd_id, 202);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_toolbar_button_autoradio(void) {
    TEST("win_toolbar_button: BUTTON_AUTORADIO clears siblings on click");

    test_env_init();
    test_env_enable_tracking(true);
    test_env_clear_events();
    reset_state();

    window_t *parent = test_env_create_window("Parent", 0, 0, 200, 200,
                                               cmd_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    uint32_t flags = WINDOW_NOTITLE | WINDOW_NOFILL | BUTTON_AUTORADIO;

    rect_t fr0 = {0, 0, 20, 20};
    window_t *b0 = create_window("A", flags, &fr0, parent, win_toolbar_button, 0, NULL);
    rect_t fr1 = {25, 0, 20, 20};
    window_t *b1 = create_window("B", flags, &fr1, parent, win_toolbar_button, 0, NULL);
    rect_t fr2 = {50, 0, 20, 20};
    window_t *b2 = create_window("C", flags, &fr2, parent, win_toolbar_button, 0, NULL);
    ASSERT_NOT_NULL(b0);
    ASSERT_NOT_NULL(b1);
    ASSERT_NOT_NULL(b2);

    // Pre-select b0
    b0->value = true;

    // Click b1 — b0 should be deselected, b1 selected
    simulate_click(b1);

    ASSERT_FALSE(b0->value);
    ASSERT_TRUE(b1->value);
    ASSERT_FALSE(b2->value);

    // Click b2 — b1 should be deselected, b2 selected
    simulate_click(b2);

    ASSERT_FALSE(b0->value);
    ASSERT_FALSE(b1->value);
    ASSERT_TRUE(b2->value);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_toolbar_button_set_image_sanity(void) {
    TEST("win_toolbar_button: btnSetImage doesn't break click behavior");

    test_env_init();
    test_env_enable_tracking(true);
    test_env_clear_events();
    reset_state();

    window_t *parent = test_env_create_window("Parent", 0, 0, 200, 200,
                                               cmd_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    rect_t fr = {10, 10, 20, 20};
    window_t *btn = create_window("T", WINDOW_NOTITLE | WINDOW_NOFILL,
                                  &fr, parent, win_toolbar_button, 0, NULL);
    ASSERT_NOT_NULL(btn);
    btn->id = 203;

    // Set an image using a dummy strip (no real GL texture; paint won't run
    // in headless tests so tex=0 is harmless).
    bitmap_strip_t strip = {
        .tex     = 0,
        .icon_w  = 16,
        .icon_h  = 16,
        .cols    = 2,
        .sheet_w = 32,
        .sheet_h = 160,
    };
    printf("Sending btnSetImage with dummy strip...\n");
    result_t r = send_message(btn, btnSetImage, 5, &strip);
    ASSERT_TRUE(r); // message was accepted

    printf("Checking that strip data was stored in button...\n");
    // Userdata should now hold the button data.
    ASSERT_NOT_NULL(btn->userdata);

    printf("Simulating click and verifying it still works...\n");
    // A click must still fire normally.
    simulate_click(btn);
    ASSERT_EQUAL(g_click_count, 1);
    ASSERT_EQUAL(g_last_cmd_id, 203);   

    printf("Clearing image with btnSetImage and NULL lparam...\n");
    // Clearing the image (lparam=NULL) should also work and release userdata.
    r = send_message(btn, btnSetImage, 0, NULL);
    ASSERT_TRUE(r);
    ASSERT_NULL(btn->userdata);
    printf("Simulating click after clearing image...\n");
    // Click still works after clearing.
    reset_state();
    simulate_click(btn);
    ASSERT_EQUAL(g_click_count, 1);
    printf("All assertions passed for this test.\n");
    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_toolbar_button_set_image_zero_cols_no_crash(void) {
    TEST("win_toolbar_button: strip with cols=0 stores safely and click still works");

    test_env_init();
    test_env_enable_tracking(true);
    test_env_clear_events();
    reset_state();

    window_t *parent = test_env_create_window("Parent", 0, 0, 200, 200,
                                               cmd_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    rect_t fr = {10, 10, 20, 20};
    window_t *btn = create_window("T", WINDOW_NOTITLE | WINDOW_NOFILL,
                                  &fr, parent, win_toolbar_button, 0, NULL);
    ASSERT_NOT_NULL(btn);
    btn->id = 204;

    // A strip with cols=0 is invalid geometry but must not cause btnSetImage
    // to crash or reject the call.  The paint handler guards against cols==0 before
    // computing index%cols, so no division-by-zero occurs when the button is drawn.
    bitmap_strip_t bad_strip = {
        .tex     = 0,
        .icon_w  = 16,
        .icon_h  = 16,
        .cols    = 0,  // invalid — paint handler must guard this
        .sheet_w = 0,
        .sheet_h = 0,
    };
    result_t r = send_message(btn, btnSetImage, 0, &bad_strip);
    ASSERT_TRUE(r);
    ASSERT_NOT_NULL(btn->userdata);

    // A click must still fire normally even with an invalid strip stored.
    simulate_click(btn);
    ASSERT_EQUAL(g_click_count, 1);
    ASSERT_EQUAL(g_last_cmd_id, 204);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// ---- main --------------------------------------------------------------- //

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    TEST_START("win_toolbar_button tests");

    test_toolbar_button_click();
    test_toolbar_button_keyboard_return();
    test_toolbar_button_keyboard_space();
    test_toolbar_button_autoradio();
    test_toolbar_button_set_image_sanity();
    test_toolbar_button_set_image_zero_cols_no_crash();

    TEST_END();
}
