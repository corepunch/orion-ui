// Basic sanity tests
// Tests for basic macros and structures without requiring SDL initialization
// Inspired by Windows 1.0 basic functionality tests

#include "test_framework.h"
#include "../ui.h"

// Test constants
#define TEST_DWORD_VALUE 0x12345678

// Test LOWORD and HIWORD macros
void test_word_macros(void) {
    TEST("LOWORD/HIWORD macros");
    
    uint32_t value = TEST_DWORD_VALUE;
    
    ASSERT_EQUAL(LOWORD(value), 0x5678);
    ASSERT_EQUAL(HIWORD(value), 0x1234);
    
    PASS();
}

// Test MAKEDWORD macro
void test_makedword_macro(void) {
    TEST("MAKEDWORD macro");
    
    uint16_t low = 0x5678;
    uint16_t high = 0x1234;
    
    uint32_t result = MAKEDWORD(low, high);
    
    ASSERT_EQUAL(result, TEST_DWORD_VALUE);
    ASSERT_EQUAL(LOWORD(result), low);
    ASSERT_EQUAL(HIWORD(result), high);
    
    PASS();
}

// Test MIN/MAX macros
void test_min_max_macros(void) {
    TEST("MIN/MAX macros");
    
    ASSERT_EQUAL(MIN(5, 10), 5);
    ASSERT_EQUAL(MIN(10, 5), 5);
    ASSERT_EQUAL(MIN(7, 7), 7);
    
    ASSERT_EQUAL(MAX(5, 10), 10);
    ASSERT_EQUAL(MAX(10, 5), 10);
    ASSERT_EQUAL(MAX(7, 7), 7);
    
    PASS();
}

// Test rectangle structure
void test_rect_structure(void) {
    TEST("Rectangle structure");
    
    rect_t rect = {10, 20, 100, 50};
    
    ASSERT_EQUAL(rect.x, 10);
    ASSERT_EQUAL(rect.y, 20);
    ASSERT_EQUAL(rect.w, 100);
    ASSERT_EQUAL(rect.h, 50);
    
    PASS();
}

// Test window message constants
void test_message_constants(void) {
    TEST("Window message constants");
    
    // Verify message constants are defined
    ASSERT_TRUE(evCreate >= 0);
    ASSERT_TRUE(evDestroy > evCreate);
    ASSERT_TRUE(evPaint > 0);
    ASSERT_TRUE(evCommand > 0);
    ASSERT_TRUE(evUser == 1000);
    
    PASS();
}

// Test control message constants
void test_control_message_constants(void) {
    TEST("Control message constants");
    
    // Verify control message constants
    ASSERT_TRUE(btnSetCheck >= evUser);
    ASSERT_TRUE(btnGetCheck > btnSetCheck);
    ASSERT_TRUE(cbAddString > 0);
    ASSERT_TRUE(cbGetCurrentSelection > 0);
    ASSERT_TRUE(cbSetCurrentSelection > 0);
    
    PASS();
}

// Test notification message constants
void test_notification_constants(void) {
    TEST("Notification message constants");
    
    // Verify notification constants
    ASSERT_TRUE(edUpdate == 100);
    ASSERT_TRUE(btnClicked > edUpdate);
    ASSERT_TRUE(cbSelectionChange > btnClicked);
    
    PASS();
}

// Test window flags
void test_window_flags(void) {
    TEST("Window flag constants");
    
    // Verify window flags are bitwise distinct
    ASSERT_EQUAL(WINDOW_NOTITLE, (1 << 0));
    ASSERT_EQUAL(WINDOW_TRANSPARENT, (1 << 1));
    ASSERT_EQUAL(WINDOW_VSCROLL, (1 << 2));
    ASSERT_EQUAL(WINDOW_HSCROLL, (1 << 3));
    
    PASS();
}

// Test zero-sized rectangles
void test_zero_rect(void) {
    TEST("Zero-sized rectangle");
    
    rect_t rect = {10, 10, 0, 0};
    
    ASSERT_EQUAL(rect.w, 0);
    ASSERT_EQUAL(rect.h, 0);
    
    PASS();
}

// Test negative coordinates
void test_negative_coords(void) {
    TEST("Negative coordinates");
    
    rect_t rect = {-10, -10, 100, 100};
    
    ASSERT_EQUAL(rect.x, -10);
    ASSERT_EQUAL(rect.y, -10);
    
    PASS();
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    TEST_START("Basic Functionality");
    
    test_word_macros();
    test_makedword_macro();
    test_min_max_macros();
    test_rect_structure();
    test_message_constants();
    test_control_message_constants();
    test_notification_constants();
    test_window_flags();
    test_zero_rect();
    test_negative_coords();
    
    TEST_END();
}
