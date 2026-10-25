// ledger_runtime_forms_test.c — every form of ledger.orion loaded at run time
// by form_library_load() equals the one orionc compiled, field by field.
// Forms that need compiled command ids (toolbars, activity pages, context
// menus) are the only ones the loader may leave out.

#include "test_framework.h"
#include "apps/ledger/ledger.h"
#include <orion/commctl/form_loader.h>

#define SHARE "apps/ledger/share/configurations"

static ledger_t g_test_ledger;

static void setup(void) {
  DB_CLASS(db_schema_xml);
  DB_CLASS(ledger_ib);
  memset(&g_test_ledger, 0, sizeof(g_test_ledger));
  g_ledger = &g_test_ledger;
  g_ledger->catalog = create_database_with_schema("catalog", "db_schema_xml", &ledger_catalog_schema, SHARE "/index.xml");
  g_ledger->cfg = create_database_with_schema("cfg", "db_schema_xml", &ledger_cfg_schema, SHARE "/ru-2003/configuration.xml");
  g_ledger->ib = create_database_with_schema("ib", "ledger_ib", &ledger_ib_schema, SHARE "/ru-2003/demo.xml");
  register_database("catalog", g_ledger->catalog);
  register_database("cfg", g_ledger->cfg);
  register_database("ib", g_ledger->ib);
}

static bool str_same(const char *a, const char *b) { return (!a && !b) || (a && b && !strcmp(a, b)); }

static int index_of(const form_def_t *f, uint32_t id) {
  for (int i = 0; i < f->child_count; i++) if (f->children[i].id == id) return i;
  return -1;
}

static char g_why[512];
#define DIFF(...) do { snprintf(g_why, sizeof(g_why), __VA_ARGS__); return false; } while (0)

static bool strings_same(const char *const *a, const char *const *b) {
  for (int i = 0; ; i++) {
    if (!str_same(a[i], b[i])) return false;
    if (!a[i]) return true;
  }
}

static bool ints_same(const int *a, const int *b) {
  for (int i = 0; ; i++) {
    if (a[i] != b[i]) return false;
    if (a[i] == -1) return true;
  }
}

static bool tableview_same(const form_def_t *gf, const form_def_t *rf, const tableview_params_t *g, const tableview_params_t *r) {
  if (g->table_id != r->table_id) DIFF("table_id %d vs %d", g->table_id, r->table_id);
  if (g->filter_field != r->filter_field || g->filter_value != r->filter_value) DIFF("filter");
  if (!strings_same(g->field_names, r->field_names)) DIFF("column fields");
  if (!strings_same(g->column_titles, r->column_titles)) DIFF("column titles");
  if (!ints_same(g->column_widths, r->column_widths) || !ints_same(g->column_min_widths, r->column_min_widths)) DIFF("column widths");
  if (!str_same(g->check_field, r->check_field)) DIFF("check_field");
  if (index_of(gf, g->master_id) != index_of(rf, r->master_id)) DIFF("master");
  if (g->master_filter_field != r->master_filter_field || !str_same(g->master_key, r->master_key)) DIFF("master key");
  if (g->cell_style != r->cell_style || g->hide_column_titles != r->hide_column_titles || g->blank_zeros != r->blank_zeros) DIFF("style");
  if (!g->record_form != !r->record_form || (g->record_form && !str_same(g->record_form->id, r->record_form->id))) DIFF("record_form");
  if (g->row_icon_count != r->row_icon_count) DIFF("row icon count");
  for (int i = 0; i < g->row_icon_count; i++) {
    const tableview_row_icon_t *a = &g->row_icons[i], *b = &r->row_icons[i];
    if (!str_same(a->field, b->field) || !str_same(a->value, b->value) || !str_same(a->icon, b->icon) || a->color != b->color) DIFF("row icon %d", i);
  }
  if (!g->column_totals != !r->column_totals) DIFF("totals");
  for (int i = 0; g->column_totals && g->field_names[i]; i++) if (g->column_totals[i] != r->column_totals[i]) DIFF("total %d", i);
  if (!str_same(g->totals_label, r->totals_label) || !str_same(g->totals_skip, r->totals_skip)) DIFF("totals label");
  return true;
}

static bool lparam_same(const form_def_t *gf, const form_def_t *rf, const form_ctrl_def_t *g, const form_ctrl_def_t *r) {
  if (!g->lparam || !r->lparam) {
    if (g->lparam != r->lparam) DIFF("lparam presence");
    return true;
  }
  if (!strcasecmp(g->class_name, "TableView")) return tableview_same(gf, rf, g->lparam, r->lparam);
  if (!strcasecmp(g->class_name, "ComboBox")) {
    const combobox_params_t *a = g->lparam, *b = r->lparam;
    if (a->table_id != b->table_id || !str_same(a->display_field, b->display_field) || !str_same(a->value_field, b->value_field) ||
        !str_same(a->filter_field, b->filter_field) || !str_same(a->filter_value, b->filter_value)) DIFF("combobox params");
    return true;
  }
  if (!strcasecmp(g->class_name, "TabView")) {
    if (((const tabview_params_t *)g->lparam)->style != ((const tabview_params_t *)r->lparam)->style) DIFF("tab style");
    return true;
  }
  if (!strcasecmp(g->class_name, "SplitView")) {
    if ((uintptr_t)g->lparam <= SPLIT_HORZ || (uintptr_t)r->lparam <= SPLIT_HORZ) {
      if (g->lparam != r->lparam) DIFF("split orientation");
      return true;
    }
    const splitview_params_t *a = g->lparam, *b = r->lparam;
    if (a->orientation != b->orientation || a->split_ratio != b->split_ratio) DIFF("split params");
    return true;
  }
  if (!strcasecmp(g->class_name, "ActivityBar")) {
    const activitybar_params_t *a = g->lparam, *b = r->lparam;
    if (a->style != b->style || a->count != b->count) DIFF("activity params");
    return true;
  }
  DIFF("lparam of unexpected class %s", g->class_name);
}

static bool control_same(const form_def_t *gf, const form_def_t *rf, int i) {
  const form_ctrl_def_t *g = &gf->children[i], *r = &rf->children[i];
  if (!str_same(g->class_name, r->class_name)) DIFF("class %s vs %s", g->class_name, r->class_name);
  if (!str_same(g->name, r->name) || !str_same(g->text, r->text)) DIFF("name/text");
  if (g->size.w != r->size.w || g->size.h != r->size.h) DIFF("size");
  if (g->flags != r->flags) DIFF("flags %llx vs %llx", (unsigned long long)g->flags, (unsigned long long)r->flags);
  if (g->h_align != r->h_align || g->v_align != r->v_align || g->layout_spacing != r->layout_spacing) DIFF("alignment/spacing");
  if (memcmp(&g->padding, &r->padding, sizeof(g->padding)) || memcmp(&g->margin, &r->margin, sizeof(g->margin))) DIFF("padding/margin");
  if (index_of(gf, g->parent) != index_of(rf, r->parent)) DIFF("parent");
  if (g->font != r->font || g->font_set != r->font_set || g->color != r->color || g->color_set != r->color_set) DIFF("font/color");
  if (!str_same(g->source, r->source) || !str_same(g->placeholder, r->placeholder)) DIFF("source/placeholder");
  if (index_of(gf, g->target) != index_of(rf, r->target) || g->target_message != r->target_message) DIFF("target");
  return lparam_same(gf, rf, g, r);
}

static bool form_same(const form_def_t *g, const form_def_t *r) {
  if (!str_same(g->name, r->name) || !str_same(g->id, r->id)) DIFF("title/id");
  if (g->width != r->width || g->height != r->height || g->flags != r->flags || g->role != r->role) DIFF("geometry/flags/role");
  if (g->layout_spacing != r->layout_spacing || memcmp(&g->padding, &r->padding, sizeof(g->padding)) ||
      memcmp(&g->margin, &r->margin, sizeof(g->margin))) DIFF("form layout");
  if (g->toolbar_style != r->toolbar_style) DIFF("toolbar style");
  if (!str_same(g->icon, r->icon) || !str_same(g->checked_icon, r->checked_icon) ||
      !str_same(g->tooltip, r->tooltip) || !str_same(g->label, r->label)) DIFF("page metadata");
  if (g->child_count != r->child_count) DIFF("child count %d vs %d", g->child_count, r->child_count);
  for (int i = 0; i < g->child_count; i++)
    if (!control_same(g, r, i)) {
      char why[sizeof(g_why)];
      snprintf(why, sizeof(why), "%s", g_why);
      DIFF("control %d (%s): %s", i, g->children[i].name ? g->children[i].name : g->children[i].class_name, why);
    }
  if (index_of(g, g->ok_id) != index_of(r, r->ok_id) || index_of(g, g->cancel_id) != index_of(r, r->cancel_id)) DIFF("ok/cancel");
  if (g->binding_count != r->binding_count) DIFF("binding count %d vs %d", g->binding_count, r->binding_count);
  for (int i = 0; i < g->binding_count; i++) {
    const ctrl_binding_t *a = &g->bindings[i], *b = &r->bindings[i];
    if (index_of(g, a->ctrl_id) != index_of(r, b->ctrl_id) || a->command != b->command || a->getter != b->getter ||
        a->offset != b->offset || a->wparam != b->wparam || a->push != b->push || a->pull != b->pull)
      DIFF("binding %d", i);
  }
  if (!str_same(g->db_name, r->db_name) || !str_same(g->db_table, r->db_table) || g->db_table_id != r->db_table_id ||
      g->db_fields != r->db_fields || g->db_field_count != r->db_field_count || g->db_record_size != r->db_record_size)
    DIFF("database binding");
  return true;
}

// What only compiled forms can express: commands named by C identifiers.
static bool needs_compiled_commands(const form_def_t *f) {
  if (f->toolbar_items || f->pane_toolbar_items || f->page_bindings) return true;
  for (int i = 0; i < f->child_count; i++) {
    if (f->children[i].context_menu) return true;
    if (!strcasecmp(f->children[i].class_name, "ActivityBar") && f->children[i].lparam &&
        ((const activitybar_params_t *)f->children[i].lparam)->count) return true;
  }
  return false;
}

static void test_runtime_forms_equal_compiled(void) {
  TEST("every runtime form equals the compiled one; only command-bound forms are left out");
  form_library_t *lib = form_library_load("apps/ledger/ledger.orion");
  ASSERT_NOT_NULL(lib);
  int compared = 0, left_out = 0;
#define CHECK_FORM(id) do { \
    const form_def_t *g = &ledger_##id##_form, *r = form_library_form(lib, #id); \
    if (!r) { \
      ASSERT(needs_compiled_commands(g), "form " #id " left out without toolbars, pages or context menus"); \
      left_out++; \
    } else { \
      bool same = form_same(g, r); \
      if (!same) fprintf(stderr, "form " #id ": %s\n", g_why); \
      ASSERT(same, "runtime form " #id " equals the compiled one"); \
      compared++; \
    } \
  } while (0);
  LEDGER_FORMS(CHECK_FORM)
#undef CHECK_FORM
  ASSERT(compared >= 6, "the dialogs and editing forms load at run time");
  ASSERT_EQUAL(form_library_form_count(lib), compared);
  printf("    %d forms equal, %d left out for their commands\n", compared, left_out);
  form_library_free(lib);
  PASS();
}

static bool schema_same(const db_schema_def_t *g, const db_schema_def_t *r) {
  if (!str_same(g->name, r->name) || !str_same(g->class_name, r->class_name) || g->table_count != r->table_count) DIFF("database");
  for (int i = 0; i < g->table_count; i++) {
    const db_table_schema_t *a = &g->tables[i], *b = &r->tables[i];
    if (a->table_id != b->table_id || !str_same(a->name, b->name) || !str_same(a->model, b->model) || a->transient != b->transient)
      DIFF("table %s", a->name);
    if (a->record_size != b->record_size) DIFF("table %s: record size %zu vs %zu", a->name, a->record_size, b->record_size);
    if (a->field_count != b->field_count || a->meta_count != b->meta_count || a->join_count != b->join_count) DIFF("table %s: counts", a->name);
    for (int f = 0; f < a->meta_count; f++) {
      const db_field_meta_t *x = &a->meta[f], *y = &b->meta[f];
      if (!str_same(x->name, y->name) || x->type != y->type || x->offset != y->offset || x->length != y->length || x->precision != y->precision)
        DIFF("table %s: field %s layout", a->name, x->name);
      const db_field_schema_t *p = &a->fields[f], *q = &b->fields[f];
      if (!str_same(p->name, q->name) || p->type != q->type || p->length != q->length || p->primary_key != q->primary_key ||
          !str_same(p->relation_table, q->relation_table) || !str_same(p->relation_field, q->relation_field) ||
          !str_same(p->default_value, q->default_value) || p->delete_rule != q->delete_rule)
        DIFF("table %s: field %s schema", a->name, p->name);
    }
    for (int k = 0; k < a->join_count; k++) {
      const db_join_schema_t *x = &a->joins[k], *y = &b->joins[k];
      if (!str_same(x->name, y->name) || !str_same(x->local_field, y->local_field) ||
          !str_same(x->foreign_table, y->foreign_table) || !str_same(x->foreign_field, y->foreign_field))
        DIFF("table %s: join %s", a->name, x->name);
    }
  }
  return true;
}

static void test_runtime_schemas_equal_compiled(void) {
  TEST("runtime database schemas lay records out as the compiled structs");
  form_library_t *lib = form_library_load("apps/ledger/ledger.orion");
  ASSERT_EQUAL(form_library_schema_count(lib), 3);
  const db_schema_def_t *compiled[] = { &ledger_cfg_schema, &ledger_catalog_schema, &ledger_ib_schema };
  for (size_t i = 0; i < ARRAY_LEN(compiled); i++) {
    const db_schema_def_t *r = form_library_schema(lib, compiled[i]->name);
    ASSERT_NOT_NULL(r);
    bool same = schema_same(compiled[i], r);
    if (!same) fprintf(stderr, "database %s: %s\n", compiled[i]->name, g_why);
    ASSERT(same, "runtime schema equals the compiled one");
  }
  form_library_free(lib);
  PASS();
}

static void test_runtime_form_opens_record(void) {
  TEST("a runtime record form opens a dialog bound to the infobase");
  form_library_t *lib = form_library_load("apps/ledger/ledger.orion");
  const form_def_t *form = form_library_form(lib, "counterparty");
  ASSERT_NOT_NULL(form);
  ASSERT_STR_EQUAL(form->db_name, "ib");
  ASSERT_EQUAL(form->db_table_id, TABLE_COUNTERPARTIES);
  uint32_t name = form_library_control_id(lib, form, "name");
  ASSERT(name >= FORM_LIBRARY_ID_BASE, "controls get ids in the runtime range");
  ASSERT_NOT_NULL(form_library_control(lib, form, name));
  form_library_free(lib);
  PASS();
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  TEST_START("Ledger runtime forms");
  setup();
  test_runtime_forms_equal_compiled();
  test_runtime_schemas_equal_compiled();
  test_runtime_form_opens_record();
  destroy_database(g_ledger->ib);
  destroy_database(g_ledger->cfg);
  destroy_database(g_ledger->catalog);
  TEST_END();
}
