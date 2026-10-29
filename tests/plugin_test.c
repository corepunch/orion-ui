#include "test_framework.h"
#include "test_env.h"
#include <orion/script/script_internal.h>
#include <lauxlib.h>
#include <orion/script/template.h>
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

static bool native_value(void *userdata, int row, const char *name, char *out, size_t size) {
  if (!strcmp(name, "title")) snprintf(out, size, "<Native>");
  else if (row > 0 && !strcmp(name, "name")) snprintf(out, size, "Row %d", row);
  else return false;
  return true;
}
static bool native_rows(void *userdata, const char *name, int parent, script_template_emit_t emit, void *target) {
  if (strcmp(name, "items")) return false;
  emit(1, target); emit(2, target);
  return true;
}
static bool native_filter(void *userdata, const char *name, const char *value, char *out, size_t size) {
  if (strcmp(name, "custom")) return false;
  snprintf(out, size, "[%s]", value);
  return true;
}
static void test_native_template(void) {
  TEST("native template source uses Lua blocks, filters and escaping; errors return no output");
  script_template_source_t source = {NULL, native_value, native_rows, native_filter};
  char *output = NULL, error[1024];
  ASSERT_TRUE(script_template_render("{{title}}{{#items}}{{n}}={{name|custom}};{{/items}}", "html", &source, &output, error, sizeof(error)));
  ASSERT_STR_EQUAL(output, "&lt;Native&gt;1=[Row 1];2=[Row 2];");
  free(output);
  ASSERT_FALSE(script_template_render("{{#items}}broken", "html", &source, &output, error, sizeof(error)));
  ASSERT_NULL(output); ASSERT_NOT_NULL(strstr(error, "unterminated"));
  ASSERT_FALSE(script_template_render("{{missing}}", "html", &source, &output, error, sizeof(error)));
  ASSERT_NULL(output); ASSERT_NOT_NULL(strstr(error, "missing"));
  ASSERT_FALSE(script_template_render("{{#unknown}}bad{{/unknown}}", "html", &source, &output, error, sizeof(error)));
  ASSERT_NULL(output);
  PASS();
}

int main(void) {
  TEST_START("Portable Lua plugins");
  test_plugins();
  test_native_template();
  TEST_END();
}
