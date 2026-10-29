#include "test_framework.h"
#include "test_env.h"
#include <orion/script/script_internal.h>
#include <lauxlib.h>
static void test_plugins(void) {
  TEST("manifests, dependencies, failures, date boundaries, money and escaping");
  lua_State *L = script_state_new("tests/fixtures/plugins", "plugin tests");
  ASSERT_NOT_NULL(L);
  ASSERT_EQUAL(luaL_loadfilex(L, "tests/fixtures/plugins/spec.lua", "t"), LUA_OK);
  char message[4096]; bool refused;
  bool ok = script_call(L, 0, 0, message, sizeof(message), &refused);
  if (!ok) fprintf(stderr, "%s\n", message);
  ASSERT_TRUE(ok);
  lua_close(L);
  PASS();
}

int main(void) {
  TEST_START("Portable Lua plugins");
  test_plugins();
  TEST_END();
}
