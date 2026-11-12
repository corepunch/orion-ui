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
  script_state_close(L);
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
  // The renderer state is reused; a second source and format render cleanly in it.
  int second = 2;
  script_template_source_t other = {&second, native_value, native_rows, NULL};
  ASSERT_TRUE(script_template_render("\"{{title}}\" {{#items}}{{name|custom}} {{/items}}", "csv", &other, &output, error, sizeof(error)));
  ASSERT_STR_EQUAL(output, "\"<Native>\" Row 1 Row 2 ");
  free(output);
  ASSERT_FALSE(script_template_render("{{#items}}broken", "html", &source, &output, error, sizeof(error)));
  ASSERT_NULL(output); ASSERT_NOT_NULL(strstr(error, "unterminated"));
  ASSERT_FALSE(script_template_render("{{missing}}", "html", &source, &output, error, sizeof(error)));
  ASSERT_NULL(output); ASSERT_NOT_NULL(strstr(error, "missing"));
  ASSERT_FALSE(script_template_render("{{#unknown}}bad{{/unknown}}", "html", &source, &output, error, sizeof(error)));
  ASSERT_NULL(output);
  PASS();
}

static void test_external_plugin(void) {
  TEST("explicit external plugin root uses the same runtime and bundled dependencies");
  const char *existing = getenv("ORION_PLUGIN_ROOT");
  char *saved = existing ? strdup(existing) : NULL;
#ifdef _WIN32
  _putenv_s("ORION_PLUGIN_ROOT", "tests/fixtures/plugins/external");
#else
  setenv("ORION_PLUGIN_ROOT", "tests/fixtures/plugins/external", 1);
#endif
  lua_State *L = script_state_new(NULL, "external plugin test");
  ASSERT_NOT_NULL(L);
  ASSERT_EQUAL(luaL_loadstring(L, "local p = require('orion.plugins').load({sdk_external=1}); assert(p.sdk_external.next_day('2026-12-31') == '2027-01-01')"), LUA_OK);
  char error[1024]; bool refused;
  ASSERT_TRUE(script_call(L, 0, 0, error, sizeof(error), &refused));
  script_state_close(L);
#ifdef _WIN32
  _putenv_s("ORION_PLUGIN_ROOT", saved ? saved : ""); free(saved);
#else
  if (saved) { setenv("ORION_PLUGIN_ROOT", saved, 1); free(saved); } else unsetenv("ORION_PLUGIN_ROOT");
#endif
  PASS();
}
int main(void) {
  TEST_START("Portable Lua plugins");
  test_plugins();
  test_native_template();
  test_external_plugin();
  TEST_END();
}
