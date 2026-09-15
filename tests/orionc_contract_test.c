// Generator contract tests for orionc.
//
// Verifies that orionc rejects invalid action manifests with actionable
// diagnostics and a non-zero exit status: unknown command references,
// duplicate action names, duplicate hotkeys, and malformed hotkey strings.
//
// These tests shell out to the orionc binary (build/bin/orionc), so they are
// POSIX-only and compiled out on Windows.

#include "test_framework.h"

#if !defined(_WIN32)
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

// Write fixture to a temp file, run orionc, capture combined output + exit code.
static int run_orionc(const char *fixture, const char *tag, char *out, size_t out_sz) {
    char input[512], output[512], cmd[1024];
    snprintf(input, sizeof(input), "/tmp/orionc_%s_%d.orion", tag, (int)getpid());
    snprintf(output, sizeof(output), "/tmp/orionc_%s_%d.h", tag, (int)getpid());

    FILE *f = fopen(input, "w");
    if (!f) return -1;
    fputs(fixture, f);
    fclose(f);

    snprintf(cmd, sizeof(cmd),
             "build/bin/orionc --input %s --output %s --prefix test 2>&1",
             input, output);
    FILE *p = popen(cmd, "r");
    if (!p) { remove(input); return -1; }
    size_t n = fread(out, 1, out_sz - 1, p);
    out[n] = '\0';
    int rc = pclose(p);
    FILE *generated = fopen(output, "r");
    if (generated && n + 1 < out_sz) {
        n += fread(out + n, 1, out_sz - n - 1, generated);
        out[n] = '\0';
    }
    if (generated) fclose(generated);

    remove(input);
    remove(output);
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    return -1;
}

static bool contains(const char *hay, const char *needle) {
    return strstr(hay, needle) != NULL;
}

static const char *kValid = ""
  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
  "<orion version=\"1\" name=\"t\" title=\"T\">\n"
  "  <menus>\n"
  "    <menu name=\"repo\" label=\"Repo\">\n"
  "      <item name=\"refresh\" label=\"Refresh\" shortcut=\"F5;Ctrl+F5\" />\n"
  "    </menu>\n"
  "  </menus>\n"
  "  <forms>\n"
  "    <form name=\"host\" width=\"200\" role=\"host\" flags=\"toolbar\" />\n"
  "    <form name=\"page\" width=\"200\" role=\"page\">\n"
  "      <Toolbar><Button name=\"r\" command=\"repo.refresh\" text=\"R\" /></Toolbar>\n"
  "    </form>\n"
  "    <form name=\"window\" width=\"200\"><Toolbar><Button command=\"repo.refresh\" /></Toolbar></form>\n"
  "  </forms>\n"
  "</orion>\n";

void test_valid_manifest_accepted(void) {
    TEST("orionc: valid manifest generates with exit 0");
    char out[16384] = {0};
    int rc = run_orionc(kValid, "valid", out, sizeof(out));
    ASSERT_EQUAL(rc, 0);
    ASSERT_TRUE(contains(out, ".role = WINDOW_ROLE_HOST"));
    ASSERT_TRUE(contains(out, ".role = WINDOW_ROLE_PAGE"));
    ASSERT_TRUE(contains(out, "test_page_toolbar"));
    ASSERT_TRUE(contains(out, ".toolbar_items = test_page_toolbar"));
    ASSERT_TRUE(contains(out, ".flags = (0) | WINDOW_TOOLBAR | WINDOW_AUTO_LAYOUT"));
    PASS();
}

void test_top_level_toolbars_rejected(void) {
    TEST("orionc: top-level toolbars fail generation");
    const char *fix = ""
      "<orion version=\"1\" name=\"t\" title=\"T\">\n"
      "  <toolbars><toolbar name=\"main\" /></toolbars>\n"
      "</orion>\n";
    char out[2048] = {0};
    int rc = run_orionc(fix, "globaltoolbar", out, sizeof(out));
    ASSERT_TRUE(rc != 0);
    ASSERT_TRUE(contains(out, "top-level <toolbars> is invalid"));
    PASS();
}

void test_toolbar_reference_rejected(void) {
    TEST("orionc: form toolbar reference fails generation");
    const char *fix = ""
      "<orion version=\"1\" name=\"t\" title=\"T\">\n"
      "  <forms><form name=\"main\" width=\"100\" toolbar=\"main\" /></forms>\n"
      "</orion>\n";
    char out[2048] = {0};
    int rc = run_orionc(fix, "toolbarref", out, sizeof(out));
    ASSERT_TRUE(rc != 0);
    ASSERT_TRUE(contains(out, "toolbar= is invalid"));
    PASS();
}

void test_unknown_form_role_rejected(void) {
    TEST("orionc: unknown form role fails generation");
    const char *fix = ""
      "<orion version=\"1\" name=\"t\" title=\"T\">\n"
      "  <forms><form name=\"bad\" width=\"100\" role=\"workspace\" /></forms>\n"
      "</orion>\n";
    char out[2048] = {0};
    int rc = run_orionc(fix, "badrole", out, sizeof(out));
    ASSERT_TRUE(rc != 0);
    ASSERT_TRUE(contains(out, "invalid role"));
    PASS();
}

void test_unknown_reference_rejected(void) {
    TEST("orionc: unknown command reference fails generation");
    const char *fix = ""
      "<orion version=\"1\" name=\"t\" title=\"T\">\n"
      "  <menus><menu name=\"repo\" label=\"Repo\"><item name=\"refresh\" label=\"Refresh\" /></menu></menus>\n"
      "  <forms><form name=\"main\" width=\"100\"><Toolbar><Button name=\"r\" command=\"repo.refreshx\" text=\"R\" /></Toolbar></form></forms>\n"
      "</orion>\n";
    char out[2048] = {0};
    int rc = run_orionc(fix, "unknown", out, sizeof(out));
    ASSERT_TRUE(rc != 0);
    ASSERT_TRUE(contains(out, "unknown command reference"));
    ASSERT_TRUE(contains(out, "repo.refreshx"));
    PASS();
}

void test_duplicate_action_name_rejected(void) {
    TEST("orionc: duplicate action name fails generation");
    const char *fix = ""
      "<orion version=\"1\" name=\"t\" title=\"T\">\n"
      "  <menus><menu name=\"repo\" label=\"Repo\">\n"
      "    <item name=\"refresh\" label=\"Refresh\" />\n"
      "    <item name=\"refresh\" label=\"Refresh Again\" />\n"
      "  </menu></menus>\n"
      "</orion>\n";
    char out[2048] = {0};
    int rc = run_orionc(fix, "dupname", out, sizeof(out));
    ASSERT_TRUE(rc != 0);
    ASSERT_TRUE(contains(out, "duplicate action name"));
    PASS();
}

void test_duplicate_hotkey_rejected(void) {
    TEST("orionc: duplicate hotkey fails generation");
    const char *fix = ""
      "<orion version=\"1\" name=\"t\" title=\"T\">\n"
      "  <menus><menu name=\"repo\" label=\"Repo\">\n"
      "    <item name=\"refresh\" label=\"Refresh\" shortcut=\"F5\" />\n"
      "    <item name=\"search\" label=\"Search\" shortcut=\"F5\" />\n"
      "  </menu></menus>\n"
      "</orion>\n";
    char out[2048] = {0};
    int rc = run_orionc(fix, "duphotkey", out, sizeof(out));
    ASSERT_TRUE(rc != 0);
    ASSERT_TRUE(contains(out, "duplicate hotkey"));
    PASS();
}

void test_malformed_hotkey_rejected(void) {
    TEST("orionc: malformed hotkey fails generation");
    const char *fix = ""
      "<orion version=\"1\" name=\"t\" title=\"T\">\n"
      "  <menus><menu name=\"repo\" label=\"Repo\">\n"
      "    <item name=\"refresh\" label=\"Refresh\" shortcut=\"Ctrl+NotAKey\" />\n"
      "  </menu></menus>\n"
      "</orion>\n";
    char out[2048] = {0};
    int rc = run_orionc(fix, "badhotkey", out, sizeof(out));
    ASSERT_TRUE(rc != 0);
    ASSERT_TRUE(contains(out, "malformed hotkey"));
    PASS();
}
#endif // !_WIN32

#if !defined(_WIN32)
void test_application_toolbar_declaration(void) {
  TEST("orionc: application toolbar is independent of forms and validates presentation");
  char out[12000] = {0};
  const char *valid = "<orion><menus><menu name=\"file\" label=\"File\">"
    "<item name=\"new\" label=\"New\" /></menu></menus>"
    "<toolbar presentation=\"compact\"><Button command=\"file.new\" icon=\"page-plus\" /></toolbar></orion>";
  ASSERT_EQUAL(run_orionc(valid, "app_toolbar", out, sizeof(out)), 0);
  ASSERT_TRUE(contains(out, "application_toolbar_t test_application_toolbar"));
  ASSERT_TRUE(contains(out, "TOOLBAR_PRESENTATION_COMPACT"));
  ASSERT_FALSE(contains(out, "form_def_t"));
  ASSERT_TRUE(run_orionc("<orion><toolbar presentation=\"tiny\" /></orion>", "bad_presentation", out, sizeof(out)) != 0);
  ASSERT_TRUE(run_orionc("<orion><toolbar/><toolbar/></orion>", "duplicate_toolbar", out, sizeof(out)) != 0);
  ASSERT_TRUE(run_orionc("<orion><toolbar><Button command=\"file.missing\" /></toolbar></orion>", "bad_app_command", out, sizeof(out)) != 0);
  ASSERT_EQUAL(run_orionc("<orion><toolbar/></orion>", "default_toolbar", out, sizeof(out)), 0);
  ASSERT_TRUE(contains(out, "TOOLBAR_PRESENTATION_NORMAL"));
  PASS();
}
#endif

int main(void) {
    TEST_START("orionc generator contract");
#if !defined(_WIN32)
    test_valid_manifest_accepted();
    test_application_toolbar_declaration();
    test_top_level_toolbars_rejected();
    test_toolbar_reference_rejected();
    test_unknown_form_role_rejected();
    test_unknown_reference_rejected();
    test_duplicate_action_name_rejected();
    test_duplicate_hotkey_rejected();
    test_malformed_hotkey_rejected();
#else
    (void)0;
#endif
    TEST_END();
}
