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
static int run_orionc_form(const char *fixture, const char *tag, char *out, size_t out_sz, const char *form) {
    char input[512], output[512], cmd[1200];
    snprintf(input, sizeof(input), "/tmp/orionc_%s_%d.orion", tag, (int)getpid());
    snprintf(output, sizeof(output), "/tmp/orionc_%s_%d.h", tag, (int)getpid());

    FILE *f = fopen(input, "w");
    if (!f) return -1;
    fputs(fixture, f);
    fclose(f);

    snprintf(cmd, sizeof(cmd),
             "build/bin/orionc --input %s --output %s --prefix test %s%s 2>&1",
             input, output, form ? "--form " : "", form ? form : "");
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

static int run_orionc(const char *fixture, const char *tag, char *out, size_t out_sz) {
  return run_orionc_form(fixture, tag, out, out_sz, NULL);
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
  "      <item name=\"rewind\" label=\"Rewind\" shortcut=\"Home\" />\n"
  "      <item name=\"forward\" label=\"Go to End\" shortcut=\"End\" />\n"
  "      <item name=\"previous\" label=\"Previous\" shortcut=\"Left;Up\" />\n"
  "      <item name=\"next\" label=\"Next\" shortcut=\"Right;Down\" />\n"
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
    ASSERT_TRUE(contains(out, "{ FVIRTKEY, AX_KEY_HOME, ID_REPO_REWIND }"));
    ASSERT_TRUE(contains(out, "{ FVIRTKEY, AX_KEY_END, ID_REPO_FORWARD }"));
    ASSERT_TRUE(contains(out, "{ FVIRTKEY, AX_KEY_LEFTARROW, ID_REPO_PREVIOUS }"));
    ASSERT_TRUE(contains(out, "{ FVIRTKEY, AX_KEY_UPARROW, ID_REPO_PREVIOUS }"));
    ASSERT_TRUE(contains(out, "{ FVIRTKEY, AX_KEY_RIGHTARROW, ID_REPO_NEXT }"));
    ASSERT_TRUE(contains(out, "{ FVIRTKEY, AX_KEY_DOWNARROW, ID_REPO_NEXT }"));
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
    "<toolbar presentation=\"compact\"><Button command=\"file.new\" icon=\"page-plus\" checked-icon=\"page\" /></toolbar></orion>";
  ASSERT_EQUAL(run_orionc(valid, "app_toolbar", out, sizeof(out)), 0);
  ASSERT_TRUE(contains(out, "application_toolbar_t test_application_toolbar"));
  ASSERT_TRUE(contains(out, "\"page-plus\", 0, 0, NULL, NULL, 0, \"page\" }"));
  ASSERT_TRUE(contains(out, "TOOLBAR_PRESENTATION_COMPACT"));
  ASSERT_FALSE(contains(out, "form_def_t"));
  ASSERT_TRUE(run_orionc("<orion><toolbar presentation=\"tiny\" /></orion>", "bad_presentation", out, sizeof(out)) != 0);
  ASSERT_TRUE(run_orionc("<orion><toolbar/><toolbar/></orion>", "duplicate_toolbar", out, sizeof(out)) != 0);
  ASSERT_TRUE(run_orionc("<orion><toolbar><Button command=\"file.missing\" /></toolbar></orion>", "bad_app_command", out, sizeof(out)) != 0);
  ASSERT_EQUAL(run_orionc("<orion><toolbar/></orion>", "default_toolbar", out, sizeof(out)), 0);
  ASSERT_TRUE(contains(out, "TOOLBAR_PRESENTATION_NORMAL"));
  PASS();
}
static void test_content_column_width(void) {
  TEST("orionc: auto column width preserves intrinsic sizing");
  char out[12000] = {0};
  const char *fixture = "<orion><forms><form name=\"resize\" width=\"210\"><GridView name=\"fields\">"
    "<Column name=\"labels\" width=\"auto\"><Label text=\"Height:\" /></Column>"
    "<Column name=\"inputs\"><TextBox name=\"height\" /></Column>"
    "</GridView></form></forms></orion>";
  ASSERT_EQUAL(run_orionc(fixture, "content_column", out, sizeof(out)), 0);
  ASSERT_TRUE(contains(out, "ID_RESIZE_LABELS, { -1, 0 }"));
  ASSERT_TRUE(contains(out, "ID_RESIZE_INPUTS, { 0, 0 }"));
  PASS();
}

static void test_activity_page_bindings(void) {
  TEST("orionc: activity bindings resolve forward forms and reject invalid navigation");
  const char *format = "<orion><forms><form name=\"main\" width=\"400\" role=\"%s\" active-page=\"%s\">"
    "<ActivityBar name=\"activity\"><Toolbar>"
    "<Button name=\"first\" page=\"%s\" focus=\"%s\"/>%s</Toolbar></ActivityBar></form>"
    "<form name=\"details\" width=\"400\" role=\"%s\"><TextEdit name=\"editor\"/></form>"
    "<form name=\"unrelated\" width=\"100\"/></forms></orion>";
  char fixture[2048], out[16000];
  snprintf(fixture, sizeof(fixture), format, "host", "details", "details", "editor", "", "page");
  ASSERT_EQUAL(run_orionc(fixture, "pages", out, sizeof(out)), 0);
  ASSERT_TRUE(contains(out, "{ ID_MAIN_FIRST, ID_MAIN_ACTIVITY, \"details\", &test_details_form, ID_DETAILS_EDITOR }"));
  ASSERT_TRUE(contains(out, ".initial_page = ID_MAIN_FIRST"));
  ASSERT_TRUE(contains(out, ".page_binding_count = 1"));
  ASSERT_EQUAL(run_orionc_form(fixture, "pages_only", out, sizeof(out), "main"), 0);
  ASSERT_TRUE(contains(out, "static const form_def_t test_details_form ="));
  ASSERT_FALSE(contains(out, "test_unrelated_form"));
  const char *cases[][7] = {
    {"normal", "details", "details", "editor", "", "page",   "require role=host"},
    {"host", "missing", "details", "editor", "", "page",    "unknown active-page"},
    {"host", "details", "missing", "editor", "", "page",    "unknown activity page"},
    {"host", "details", "details", "missing", "", "page",   "unknown activity focus control"},
    {"host", "details", "details", "editor", "", "normal",  "requires role=page"},
    {"host", "details", "details", "editor", "<Button name=\"second\" page=\"details\"/>", "page", "duplicate activity"},
    {"host", "details", "details", "editor", "<Button name=\"second\"/>", "page", "requires page="},
  };
  for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
    snprintf(fixture, sizeof(fixture), format, cases[i][0], cases[i][1], cases[i][2], cases[i][3], cases[i][4], cases[i][5]);
    ASSERT_TRUE(run_orionc(fixture, "bad_pages", out, sizeof(out)) != 0);
    ASSERT_TRUE(contains(out, cases[i][6]));
  }
  PASS();
}

static void test_page_metadata(void) {
  TEST("orionc: Page entries derive navigation from page forms");
  const char *format = "<orion><forms><form name=\"main\" width=\"400\" role=\"host\">"
    "<ActivityBar name=\"activity\">%s</ActivityBar></form>"
    "<form name=\"details\" title=\"Details\" width=\"400\" role=\"page\" focus=\"editor\" "
    "icon=\"document\" checked-icon=\"document-fill\" tooltip=\"Document details\"><TextEdit name=\"editor\"/></form>"
    "<form name=\"unrelated\" width=\"100\"/></forms></orion>";
  char fixture[2048], out[16000];
  snprintf(fixture, sizeof(fixture), format, "<Page form=\"details\"/>");
  ASSERT_EQUAL(run_orionc_form(fixture, "page_metadata", out, sizeof(out), "main"), 0);
  ASSERT_TRUE(contains(out, "{ ID_MAIN_DETAILS, ID_MAIN_ACTIVITY, \"details\", &test_details_form, ID_DETAILS_EDITOR }"));
  ASSERT_TRUE(contains(out, ".initial_page = ID_MAIN_DETAILS"));
  ASSERT_TRUE(contains(out, ".icon = \"document\", .checked_icon = \"document-fill\", .tooltip = \"Document details\""));
  ASSERT_FALSE(contains(out, "test_unrelated_form"));
  ASSERT_FALSE(contains(out, "{ \"Page\","));
  const char *bad[] = {"<Page/>", "<Page form=\"missing\"/>",
    "<Page form=\"details\"/><Page form=\"details\"/>",
    "<Page form=\"details\"/><Toolbar><Button page=\"details\"/></Toolbar>"};
  for (int i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); i++) {
    snprintf(fixture, sizeof(fixture), format, bad[i]);
    ASSERT_TRUE(run_orionc(fixture, "invalid_page_metadata", out, sizeof(out)) != 0);
  }
  PASS();
}

static void test_activitybar_declaration(void) {
  TEST("orionc: nested activity buttons share validated commands and stay out of the form child tree");
  char out[12000] = {0};
  const char *fixture = "<orion><menus><menu name=\"view\" label=\"View\">"
    "<item name=\"overview\" label=\"Overview\" /></menu></menus>"
    "<forms><form name=\"main\" width=\"400\"><ActivityBar name=\"activity\"><Toolbar>"
    "<Button command=\"view.overview\" icon=\"view-grid\" checked-icon=\"view-grid-fill\" text=\"Overview\" />"
    "<Button name=\"filter\" icon=\"filter\" tooltip=\"Filter\" />"
    "</Toolbar></ActivityBar></form></forms></orion>";
  ASSERT_EQUAL(run_orionc(fixture, "activity", out, sizeof(out)), 0);
  ASSERT_TRUE(contains(out, "TOOLBAR_ITEM_BUTTON, ID_VIEW_OVERVIEW"));
  ASSERT_TRUE(contains(out, "TOOLBAR_ITEM_BUTTON, ID_MAIN_FILTER"));
  ASSERT_TRUE(contains(out, "0 | TBSTYLE_CHECKGROUP"));
  ASSERT_TRUE(contains(out, ", 0, \"view-grid-fill\""));
  ASSERT_TRUE(contains(out, "activitybar_params_t main_activity_activity_params"));
  ASSERT_TRUE(contains(out, "\"ActivityBar\", ID_MAIN_ACTIVITY"));
  ASSERT_TRUE(contains(out, ".child_count = 1"));
  const char *invalid = "<orion><forms><form name=\"main\" width=\"400\"><ActivityBar name=\"activity\">"
    "<Toolbar><Button command=\"view.missing\" /></Toolbar></ActivityBar></form></forms></orion>";
  ASSERT_TRUE(run_orionc(invalid, "activity_bad_command", out, sizeof(out)) != 0);
  ASSERT_TRUE(contains(out, "unknown command reference 'view.missing'"));
  PASS();
}

static void test_accounting_presentation_declaration(void) {
  TEST("orionc: RowIcon, total=sum, totals-skip, list labels and page labels reach the generated params");
  char out[16000] = {0};
  const char *fixture = "<orion><databases><database name=\"db\" class=\"db_schema_xml\"><table name=\"rows\">"
    "<field name=\"id\" type=\"integer\" key=\"YES\"/><field name=\"amount\" type=\"double\"/>"
    "<field name=\"sub\" type=\"boolean\"/></table></database></databases>"
    "<forms><form name=\"main\" width=\"400\" role=\"host\" toolbar-style=\"list\" flags=\"toolbar\"><Toolbar><title/></Toolbar>"
    "<ActivityBar name=\"activity\" labels=\"right\"><Page form=\"list\"/></ActivityBar></form>"
    "<form name=\"list\" label=\"Rows\" width=\"400\" role=\"page\">"
    "<TableView name=\"rows\" source=\"db.rows\" totals-skip=\"sub\">"
    "<RowIcon field=\"sub\" icon=\"folder\" color=\"secondary\"/><RowIcon icon=\"row\"/>"
    "<Column field=\"id\" title=\"No.\"/><Column field=\"amount\" title=\"Amount\" total=\"sum\"/>"
    "</TableView></form></forms></orion>";
  ASSERT_EQUAL(run_orionc(fixture, "accounting", out, sizeof(out)), 0);
  ASSERT_TRUE(contains(out, "{ \"sub\", NULL, \"folder\", brTextSecondary },"));
  ASSERT_TRUE(contains(out, "{ NULL, NULL, \"row\", -1 },"));
  ASSERT_TRUE(contains(out, "_totals[] = { false, true, };"));
  ASSERT_TRUE(contains(out, ".row_icon_count = 2"));
  ASSERT_TRUE(contains(out, ".totals_skip = \"sub\""));
  ASSERT_TRUE(contains(out, "{ NULL, 0, TOOLBAR_STYLE_LIST }"));
  ASSERT_TRUE(contains(out, ".toolbar_style = TOOLBAR_STYLE_LIST"));
  ASSERT_TRUE(contains(out, ".label = \"Rows\""));
  ASSERT_TRUE(contains(out, "{ TOOLBAR_ITEM_TITLE, 0, NULL"));
  ASSERT_FALSE(contains(out, "\"RowIcon\""));  // not a child window
  PASS();
}

static void test_pane_toolbar_declaration(void) {
  TEST("Page pane toolbars share menu commands and declare alternate actions");
  const char *xml = "<orion name=\"test\"><menus><menu name=\"pane\" label=\"Pane\">"
    "<item name=\"right\" label=\"Right\"/><item name=\"down\" label=\"Down\"/></menu></menus>"
    "<forms><form name=\"workspace\" width=\"800\" role=\"page\"><PaneToolbar replace=\"true\">"
    "<Button command=\"pane.right\" icon=\"columns\" alt-command=\"pane.down\" alt-icon=\"rows\" alt-tooltip=\"Split Down\"/>"
    "</PaneToolbar><TabView name=\"tabs\" hide-single-tab=\"true\"/></form></forms></orion>";
  char out[32768];
  ASSERT_EQUAL(run_orionc(xml, "pane_toolbar", out, sizeof(out)), 0);
  ASSERT_TRUE(contains(out, ".pane_toolbar_items = test_workspace_pane_toolbar"));
  ASSERT_TRUE(contains(out, ".pane_toolbar_replace = true"));
  ASSERT_TRUE(contains(out, "&(const tabview_params_t){ .style = TAB_STYLE_HIDE_SINGLE_TAB }"));
  ASSERT_TRUE(contains(out, ".alt_ident = ID_PANE_DOWN, .alt_icon = \"rows\", .alt_tooltip = \"Split Down\""));
  ASSERT_FALSE(contains(out, ".class_name = \"PaneToolbar\""));
  const char *invalid = "<orion name=\"test\"><forms><form name=\"main\" width=\"400\"><PaneToolbar/>"
    "</form></forms></orion>";
  ASSERT_TRUE(run_orionc(invalid, "pane_toolbar_bad_role", out, sizeof(out)) != 0);
  PASS();
}

#endif

int main(void) {
    TEST_START("orionc generator contract");
#if !defined(_WIN32)
    test_valid_manifest_accepted();
    test_activity_page_bindings();
    test_page_metadata();
    test_pane_toolbar_declaration();
    test_content_column_width();
    test_activitybar_declaration();
    test_accounting_presentation_declaration();
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
