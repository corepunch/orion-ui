#include "test_framework.h"
#include <orion/user/money.h>
#include <stdint.h>

static void test_decimal(void) {
  TEST("decimal parsing is exact, bounded and preserves output on rejection");
  int64_t v = 7;
  ASSERT_TRUE(money_parse_decimal("244.50", 2, &v)); ASSERT_EQUAL(v, 24450);
  ASSERT_TRUE(money_parse_decimal("-0.005", 3, &v)); ASSERT_EQUAL(v, -5);
  ASSERT_TRUE(money_parse_decimal("92233720368547758.07", 2, &v)); ASSERT_EQUAL(v, INT64_MAX);
  ASSERT_TRUE(money_parse_decimal("-92233720368547758.08", 2, &v)); ASSERT_EQUAL(v, INT64_MIN);
  ASSERT_FALSE(money_parse_decimal("92233720368547758.08", 2, &v)); ASSERT_EQUAL(v, INT64_MIN);
  ASSERT_FALSE(money_parse_decimal("1.001", 2, &v)); ASSERT_EQUAL(v, INT64_MIN);
  ASSERT_FALSE(money_parse_decimal("1e2", 2, &v)); ASSERT_EQUAL(v, INT64_MIN);
  PASS();
}

static void test_scaled_multiply(void) {
  TEST("scaled multiplication rounds positive and negative half cents and detects overflow");
  int64_t v = 0;
  ASSERT_TRUE(money_mul_div(24450, 700, 10000, &v)); ASSERT_EQUAL(v, 1712);
  ASSERT_TRUE(money_mul_div(-24450, 700, 10000, &v)); ASSERT_EQUAL(v, -1712);
  ASSERT_TRUE(money_mul_div(INT64_MAX, 2, 2, &v)); ASSERT_EQUAL(v, INT64_MAX);
  ASSERT_FALSE(money_mul_div(INT64_MAX, 2, 1, &v)); ASSERT_EQUAL(v, INT64_MAX);
  ASSERT_FALSE(money_mul_div(1, 1, 0, &v)); ASSERT_EQUAL(v, INT64_MAX);
  PASS();
}

int main(void) { TEST_START("Money"); test_decimal(); test_scaled_multiply(); TEST_END(); }
