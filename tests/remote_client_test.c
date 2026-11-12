#include "test_framework.h"
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <stdio.h>
static void test_remote_client(void) {
  TEST("remote client persists exact envelopes, retries and handles conflicts");
  lua_State *L = luaL_newstate(); ASSERT_NOT_NULL(L); luaL_openlibs(L);
  int result = luaL_dofile(L, "tests/fixtures/remote/client_spec.lua");
  if (result != LUA_OK) fprintf(stderr, "%s\n", lua_tostring(L, -1));
  ASSERT_EQUAL(result, LUA_OK); lua_close(L); PASS();
}
int main(void) { TEST_START("remote client"); test_remote_client(); TEST_END(); }
