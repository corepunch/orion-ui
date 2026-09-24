// Focused headless tests for the window-first FormEditor architecture.

#include "test_framework.h"
#include "test_env.h"
#include "apps/formeditor/formeditor.h"
#include "apps/formeditor/controls-icons.h"
#include <orion/commctl/commctl.h>

#include <libxml/parser.h>
#include <stdlib.h>
#include <string.h>

app_state_t *g_app = NULL;

int message_box(window_t *parent, const char *text,
                const char *caption, uint32_t type) {
  (void)parent; (void)text; (void)caption; (void)type;
  return IDCANCEL;
}

static bool load_components(void) {
  char path[4096];
  int n = snprintf(path, sizeof(path), "%s/../lib/formeditor_components%s",
                   ui_get_exe_dir(), AX_DYNLIB_EXT);
  return n > 0 && (size_t)n < sizeof(path) && fe_load_component_plugin(path);
}

static void setup(void) {
  test_env_init();
  g_app = calloc(1, sizeof(*g_app));
  if (!g_app) return;
  g_app->current_tool = ID_TOOL_SELECT;
  load_components();
  create_form_doc(FORM_DEFAULT_W, FORM_DEFAULT_H);
}

static void teardown(void) {
  if (!g_app) { test_env_shutdown(); return; }
  formeditor_close_database_object_window();
  while (g_app->form_count > 0 && g_app->forms[0]) close_form_doc(g_app->forms[0]);
  for (int i = 0; i < FE_NUM_WINDOWS; i++) {
    if (g_app->windows[i] && is_window(g_app->windows[i])) destroy_window(g_app->windows[i]);
    g_app->windows[i] = NULL;
  }
  fe_project_clear_xml();
  fe_unload_component_plugins();
  free(g_app);
  g_app = NULL;
  test_env_shutdown();
}

static int component_id(const char *name) {
  const fe_component_desc_t *wanted = fe_component_by_class_name(name);
  for (int i = 0; wanted && i < fe_component_count(); i++)
    if (fe_component_at(i) == wanted) return i;
  return -1;
}

static xmlNodePtr first_child(xmlNodePtr parent, const char *name) {
  for (xmlNodePtr node = parent ? parent->children : NULL; node; node = node->next)
    if (node->type == XML_ELEMENT_NODE &&
        xmlStrcasecmp(node->name, BAD_CAST name) == 0) return node;
  return NULL;
}

static char *attr_dup(xmlNodePtr node, const char *name) {
  xmlChar *value = node ? xmlGetProp(node, BAD_CAST name) : NULL;
  if (!value) return NULL;
  char *copy = strdup((const char *)value);
  xmlFree(value);
  return copy;
}

static window_t *descendant_by_class(window_t *parent, const char *class_name) {
  for (window_t *child = parent ? parent->children : NULL; child; child = child->next) {
    if (window_is_class(child, class_name)) return child;
    window_t *found = descendant_by_class(child, class_name);
    if (found) return found;
  }
  return NULL;
}

static window_t *paint_expected;
static int paint_expected_count;
static void count_expected_paint(window_t *win, uint32_t msg, uint32_t wparam,
                                 void *lparam, void *userdata) {
  (void)msg; (void)wparam; (void)lparam; (void)userdata;
  if (win == paint_expected) paint_expected_count++;
}

static void install_form_xml(window_t *doc, const char *xml) {
  xmlDocPtr parsed = xmlReadMemory(xml, (int)strlen(xml), "test.orion", NULL, XML_PARSE_NONET);
  xmlNodePtr root = parsed ? xmlDocGetRootElement(parsed) : NULL;
  if (doc->userdata2) xmlFreeNode((xmlNodePtr)doc->userdata2);
  doc->userdata2 = root ? xmlCopyNode(root, 1) : NULL;
  if (parsed) xmlFreeDoc(parsed);
  canvas_rebuild_live_controls(doc);
  window_layout_sync(doc);
}

static const db_field_schema_t post_fields[] = {
  {.name="id",        .type=DB_TYPE_INT,    .primary_key=true},
  {.name="title",     .type=DB_TYPE_STRING, .length=64},
  {.name="author_id", .type=DB_TYPE_INT,    .relation_table="authors", .relation_field="id"},
};
static const db_field_schema_t author_fields[] = {
  {.name="id",   .type=DB_TYPE_INT,    .primary_key=true},
  {.name="name", .type=DB_TYPE_STRING, .length=64},
};
static const db_join_schema_t post_joins[] = {
  {.name="author", .local_field="author_id", .foreign_table="authors", .foreign_field="id"},
};
static const db_table_schema_t tables[] = {
  {.table_id=1, .name="posts",   .fields=post_fields,   .field_count=ARRAY_LEN(post_fields),
   .joins=post_joins, .join_count=ARRAY_LEN(post_joins)},
  {.table_id=2, .name="authors", .fields=author_fields, .field_count=ARRAY_LEN(author_fields)},
};
static const db_schema_def_t schema = {
  .name="db", .class_name="test", .tables=tables, .table_count=ARRAY_LEN(tables),
};
static result_t object_proc(const void *object, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)object; (void)msg; (void)wparam; (void)lparam;
  return false;
}
static const db_field_msg_binding_t bindings[] = {
  {.field="id", .column_id=1}, {.field="title", .column_id=2},
  {.field="author.name", .column_id=3},
};
static lresult_t db_proc(database_t *db, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)db; (void)wparam;
  if (msg == dbGetSchema) return (lresult_t)&schema;
  if (msg == dbGetObjectProc) return (lresult_t)object_proc;
  if (msg == dbGetFieldBindings) {
    if (lparam) *(int *)lparam = ARRAY_LEN(bindings);
    return (lresult_t)bindings;
  }
  if (msg == dbFetch) return 0;
  return 0;
}

void test_window_first_document_state(void) {
  TEST("window-first document: active form and XML-backed runtime");
  setup();
  ASSERT_NOT_NULL(g_app);
  ASSERT_EQUAL(g_app->form_count, 1);
  ASSERT_TRUE(g_app->active_form == g_app->forms[0]);
  ASSERT_NOT_NULL(g_app->active_form->children);
  teardown();
  PASS();
}

void test_document_title_update_is_stable(void) {
  TEST("document title: path trimming and modified suffix are stable");
  setup();
  window_t *doc = g_app->active_form;
  snprintf(doc->title, sizeof(doc->title), "/tmp/Sample Form");
  fe_doc_state(doc)->modified = true;
  form_doc_update_title(doc); form_doc_update_title(doc);
  ASSERT_STR_EQUAL(doc->title, "Sample Form *");
  fe_doc_state(doc)->modified = false;
  form_doc_update_title(doc);
  ASSERT_STR_EQUAL(doc->title, "Sample Form");
  teardown();
  PASS();
}

void test_component_drop_persists_without_sidecar(void) {
  TEST("component drop: live window and XML are updated without element arrays");
  setup();
  window_t *doc = g_app->active_form;
  int button = component_id("Button");
  ASSERT_TRUE(button >= 0);
  ASSERT_NOT_NULL(doc->children);
  ASSERT_TRUE(canvas_drop_component_to_target(doc, button, doc->children,
      window_screen_x(doc->children) + 4, window_screen_y(doc->children) + 4));
  ASSERT_NOT_NULL(first_child((xmlNodePtr)doc->userdata2, "Button"));
  ASSERT_TRUE(fe_doc_state(doc)->modified);
  ASSERT_TRUE(g_app->project.modified);
  teardown();
  PASS();
}

void test_objects_browser_lists_forms_and_databases(void) {
  TEST("Objects browser: flat list contains forms and databases");
  setup();
  database_t db = {.name="db", .class_name="test", .proc=db_proc};
  g_app->project.databases[0] = &db;
  g_app->project.database_count = 1;
  g_app->windows[FE_WIN_FORMS] = forms_browser_create(0);
  forms_browser_refresh();
  window_t *list = g_app->windows[FE_WIN_FORMS]->children;
  ASSERT_NOT_NULL(list);
  ASSERT_EQUAL((int)send_message(list, RVM_GETITEMCOUNT, 0, NULL), 2);
  reportview_item_t form = {0}, database = {0};
  ASSERT_TRUE(send_message(list, RVM_GETITEMDATA, 0, &form));
  ASSERT_TRUE(send_message(list, RVM_GETITEMDATA, 1, &database));
  ASSERT_TRUE(strstr(form.text, "Form:") != NULL);
  ASSERT_STR_EQUAL(database.text, "Database: db");
  teardown();
  PASS();
}

void test_components_palette_uses_png_strip_indices(void) {
  TEST("Components palette: bitmap strip indices identify the original PNG icons");
  setup();
  g_app->windows[FE_WIN_TOOL] = formeditor_create_components_palette(0);
  window_t *list = g_app->windows[FE_WIN_TOOL]->children;
  ASSERT_NOT_NULL(list);
  reportview_item_t button = {0}, checkbox = {0}, label = {0};
  ASSERT_TRUE(send_message(list, RVM_GETITEMDATA, 0, &button));
  ASSERT_TRUE(send_message(list, RVM_GETITEMDATA, 1, &checkbox));
  ASSERT_TRUE(send_message(list, RVM_GETITEMDATA, 2, &label));
  ASSERT_EQUAL(button.icon, IC_BUTTON);
  ASSERT_EQUAL(checkbox.icon, IC_CHECKBOX);
  ASSERT_EQUAL(label.icon, IC_TEXT);
  ASSERT_EQUAL((int)send_message(list, RVM_HITTEST, MAKEDWORD(140, 20), NULL), 2);
  ASSERT_TRUE(button.icon_name == NULL);
  ASSERT_TRUE(checkbox.icon_name == NULL);
  ASSERT_TRUE(label.icon_name == NULL);
  teardown();
  PASS();
}

void test_database_browser_cascades(void) {
  TEST("database browser: tables cascade to fields and related fields");
  setup();
  database_t db = {.name="db", .class_name="test", .proc=db_proc};
  g_app->project.databases[0] = &db;
  g_app->project.database_count = 1;
  formeditor_show_database_object_window(0);

  window_t *wrapper = NULL;
  for (window_t *win = g_ui_runtime.windows; win; win = win->next)
    if (strcmp(win->title, "Databases") == 0) { wrapper = win; break; }
  ASSERT_NOT_NULL(wrapper);
  window_t *browser = wrapper->children;
  window_t *table_list = browser ? browser->children : NULL;
  ASSERT_NOT_NULL(table_list);
  ASSERT_EQUAL((int)send_message(table_list, RVM_GETITEMCOUNT, 0, NULL), 2);

  send_message(table_list, RVM_SETSELECTION, 0, NULL);
  send_message(browser, evCommand, 0, table_list);
  ASSERT_EQUAL((int)send_message(browser, cbGetColumnCount, 0, NULL), 2);
  window_t *field_list = table_list->next;
  ASSERT_NOT_NULL(field_list);
  ASSERT_TRUE((int)send_message(field_list, RVM_GETITEMCOUNT, 0, NULL) >= 3);

  send_message(field_list, RVM_SETSELECTION, 2, NULL);
  send_message(browser, evCommand, 0, field_list);
  ASSERT_EQUAL((int)send_message(browser, cbGetColumnCount, 0, NULL), 3);
  teardown();
  PASS();
}

void test_database_field_drop_updates_xml_column(void) {
  TEST("database drag/drop: field binds to TableView column under cursor");
  setup();
  database_t db = {.name="db", .class_name="test", .proc=db_proc};
  g_app->project.databases[0] = &db;
  g_app->project.database_count = 1;
  ui_set_database(&db);

  window_t *doc = g_app->active_form;
  install_form_xml(doc,
      "<form name=\"main\" title=\"Main\" width=\"320\" height=\"180\">"
      "<TableView name=\"feed\" source=\"db.posts\" flags=\"vscroll,flexspace\">"
      "<Column field=\"title\" title=\"Title\" width=\"80\"/>"
      "<Column field=\"id\" title=\"ID\" width=\"40\"/>"
      "</TableView></form>");
  window_t *table = descendant_by_class(doc, "TableView");
  ASSERT_NOT_NULL(table);
  ASSERT_EQUAL((int)send_message(table, RVM_GETCOLUMNCOUNT, 0, NULL), 2);
  ASSERT_NOT_NULL(fe_project_table_node_for_window(doc, table));

  fe_database_field_ref_t field = {0};
  snprintf(field.database, sizeof(field.database), "db");
  snprintf(field.table, sizeof(field.table), "authors");
  snprintf(field.field, sizeof(field.field), "name");
  char expression[128], title[128], error[256] = {0};
  ASSERT_TRUE(fe_resolve_table_column_database_field(
      fe_project_table_node_for_window(doc, table), &field,
      expression, sizeof(expression), title, sizeof(title), error, sizeof(error)));
  ASSERT_STR_EQUAL(expression, "author.name");
  int sx = window_screen_x(table) + 4;
  int sy = window_screen_y(table) + titlebar_height(table) + 4;
  ASSERT_TRUE(canvas_bind_database_field(doc, &field, sx, sy));

  xmlNodePtr table_node = first_child((xmlNodePtr)doc->userdata2, "TableView");
  xmlNodePtr column = first_child(table_node, "Column");
  char *value = attr_dup(column, "field");
  ASSERT_NOT_NULL(value);
  ASSERT_STR_EQUAL(value, "author.name");
  free(value);
  ASSERT_TRUE(fe_doc_state(doc)->modified);
  teardown();
  PASS();
}

void test_database_field_drop_rejects_other_database(void) {
  TEST("database drag/drop: cross-database binding is rejected");
  setup();
  database_t db = {.name="db", .class_name="test", .proc=db_proc};
  g_app->project.databases[0] = &db;
  g_app->project.database_count = 1;
  ui_set_database(&db);
  window_t *doc = g_app->active_form;
  install_form_xml(doc,
      "<form name=\"main\"><TableView source=\"db.posts\" flags=\"flexspace\">"
      "<Column field=\"title\" title=\"Title\" width=\"80\"/>"
      "</TableView></form>");
  window_t *table = descendant_by_class(doc, "TableView");
  fe_database_field_ref_t field = {0};
  snprintf(field.database, sizeof(field.database), "other");
  snprintf(field.table, sizeof(field.table), "authors");
  snprintf(field.field, sizeof(field.field), "name");
  ASSERT_FALSE(canvas_bind_database_field(doc, &field,
      window_screen_x(table) + 4, window_screen_y(table) + titlebar_height(table) + 4));
  teardown();
  PASS();
}

void test_socialfeed_project_loads_runtime_and_database(void) {
  TEST("SocialFeed project: plugin, database, forms, layout, and rows load");
  setup();
  ASSERT_TRUE(fe_project_load("apps/socialfeed/socialfeed.orion"));
  ASSERT_EQUAL(g_app->project.plugin_count, 1);
  ASSERT_STR_EQUAL(g_app->project.plugins[0].name, "socialfeed_components");
  ASSERT_EQUAL(g_app->project.database_count, 1);
  ASSERT_NOT_NULL(g_app->project.databases[0]);
  ASSERT_STR_EQUAL(g_app->project.databases[0]->name, "socialfeed");
  ASSERT_TRUE(ui_get_database() == g_app->project.databases[0]);
  ASSERT_EQUAL(g_app->form_count, 4);

  window_t *doc = g_app->forms[0];
  ASSERT_NOT_NULL(doc);
  ASSERT_STR_EQUAL(doc->title, "Social Feed");
  ASSERT_TRUE((doc->flags & WINDOW_TOOLBAR) != 0);
  ASSERT_TRUE((doc->flags & WINDOW_STATUSBAR) != 0);
  toolbar_state_t *toolbar = window_toolbar_state(doc);
  ASSERT_NOT_NULL(toolbar);
  ASSERT_EQUAL(toolbar->item_count, 5);
  ASSERT_STR_EQUAL(toolbar->items[2].icon, "chat-bubble-empty");
  ASSERT_NOT_NULL(doc->children);
  ASSERT_TRUE(doc->children->frame.w > 0);
  ASSERT_TRUE(doc->children->frame.h > 0);
  ASSERT_TRUE(descendant_by_class(doc, "Toolbar") == NULL);

  window_t *table = descendant_by_class(doc, "TableView");
  ASSERT_NOT_NULL(table);
  ASSERT_TRUE((table->flags & WINDOW_FLEXSPACE) != 0);
  ASSERT_TRUE((table->flags & WINDOW_VSCROLL) != 0);
  ASSERT_TRUE(table->frame.w > 0);
  ASSERT_TRUE(table->frame.h > 0);
  ASSERT_EQUAL((int)send_message(table, RVM_GETCOLUMNCOUNT, 0, NULL), 4);
  ASSERT_EQUAL((int)send_message(table, RVM_GETITEMCOUNT, 0, NULL), 5);
  paint_expected = table;
  paint_expected_count = 0;
  register_window_hook(evPaint, count_expected_paint, NULL);
  send_message(doc, evPaint, 0, NULL);
  deregister_window_hook(evPaint, count_expected_paint, NULL);
  paint_expected = NULL;
  ASSERT_TRUE(paint_expected_count > 0);

  formeditor_show_database_object_window(0);
  window_t *db_window = NULL;
  for (window_t *win = g_ui_runtime.windows; win; win = win->next)
    if (strcmp(win->title, "Databases") == 0) { db_window = win; break; }
  ASSERT_NOT_NULL(db_window);
  window_t *browser = db_window->children;
  window_t *tables_list = browser ? browser->children : NULL;
  ASSERT_NOT_NULL(tables_list);
  ASSERT_EQUAL((int)send_message(tables_list, RVM_GETITEMCOUNT, 0, NULL), 3);
  send_message(tables_list, RVM_SETSELECTION, 1, NULL);
  send_message(browser, evCommand, 0, tables_list);
  ASSERT_EQUAL((int)send_message(browser, cbGetColumnCount, 0, NULL), 2);
  window_t *fields_list = tables_list->next;
  ASSERT_NOT_NULL(fields_list);
  ASSERT_TRUE((int)send_message(fields_list, RVM_GETITEMCOUNT, 0, NULL) >= 6);

  teardown();
  PASS();
}

int main(void) {
  TEST_START("Form Editor Window-First Recovery");
  test_window_first_document_state();
  test_document_title_update_is_stable();
  test_component_drop_persists_without_sidecar();
  test_objects_browser_lists_forms_and_databases();
  test_components_palette_uses_png_strip_indices();
  test_database_browser_cascades();
  test_database_field_drop_updates_xml_column();
  test_database_field_drop_rejects_other_database();
  test_socialfeed_project_loads_runtime_and_database();
  TEST_END();
}
