#include "test_framework.h"
#include <orion/user/gl_compat.h>
#include "vendor/gl_shader/test_contract.h"

static void test_contract(void) {
  TEST("GLSL profiles, storage validation, failure cleanup and typed uniform caching");
  ASSERT_TRUE(gs_test_contract());
  PASS();
}

int main(void) {
  TEST_START("Shared shader module");
  test_contract();
  TEST_END();
}
