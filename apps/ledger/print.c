// Print forms: the configuration ships HTML templates for its legal forms
// (cfg.print_forms); this fills them from a document or from a report.
//
// Shared syntax, filters and escaping live in Orion's Lua template plugins.
#include "ledger.h"
#include <libxml/parser.h>
#include <orion/user/file.h>
#include <orion/script/template.h>

typedef struct { int table; const void *record; bool owned; } print_row_t;
typedef struct {
  const db_document_t *doc;
  print_row_t *rows;
  int count, capacity;
} print_ctx_t;

static void *first_record(database_t *db, int table_id) {
  result_node_t *rows = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(table_id, 0), NULL);
  void *record = rows ? *(void **)rows->data : NULL;
  free_result_list(rows);
  return record;
}

// Whether a field or join path starts with a field of the table, so that
// a row's fields win over the document's without reporting misses.
static bool table_has(database_t *db, int table_id, const char *name) {
  const db_table_schema_t *table = db_schema_table(db->schema, table_id);
  char field[64];
  snprintf(field, sizeof(field), "%.*s", (int)strcspn(name, "."), name);
  if (db_schema_field_index(table, field) >= 0) return true;
  if (!strchr(name, '.')) return false;
  strncat(field, "_id", sizeof(field) - strlen(field) - 1);
  return db_schema_field_index(table, field) >= 0;
}

static bool print_value(void *userdata, int row, const char *name, char *buf, size_t sz) {
  print_ctx_t *ctx = userdata;
  buf[0] = '\0';
  if (!strncmp(name, "org.", 4))
    return db_get_field_text(g_ledger->ib, TABLE_ORGANIZATIONS, first_record(g_ledger->ib, TABLE_ORGANIZATIONS), name + 4, buf, sz);
  if (!strncmp(name, "cfg.", 4))
    return db_get_field_text(g_ledger->cfg, TABLE_SETTINGS, first_record(g_ledger->cfg, TABLE_SETTINGS), name + 4, buf, sz);
  if (!strncmp(name, "period.", 7))
    return db_get_field_text(g_ledger->ib, TABLE_REPORT_SETTINGS, ledger_report_settings(g_ledger->ib, g_ledger->cfg), name + 7, buf, sz);
  if (row > 0 && row <= ctx->count && table_has(g_ledger->ib, ctx->rows[row - 1].table, name))
    return db_get_field_text(g_ledger->ib, ctx->rows[row - 1].table, ctx->rows[row - 1].record, name, buf, sz);
  return ctx->doc && db_get_field_text(g_ledger->ib, TABLE_DOCUMENTS, ctx->doc, name, buf, sz);
}

static ui_gender_t gender_of(const char *code) {
  return code[0] == 'f' ? UI_GENDER_FEMININE : code[0] == 'n' ? UI_GENDER_NEUTER : UI_GENDER_MASCULINE;
}

// Amounts in words follow the configuration's language: the form is a legal
// document of its country.
static void amount_in_words(const char *value, char *out, size_t sz) {
  const db_setting_t *s = first_record(g_ledger->cfg, TABLE_SETTINGS);
  if (!s) { snprintf(out, sz, "%s", value); return; }
  ui_money_words_t words = { s->currency_units, gender_of(s->currency_gender), s->cents_units, gender_of(s->cents_gender) };
  ui_spell_amount(s->language, ui_parse_number(value), &words, out, sz);
}

// Only country-specific language and money nouns belong to Ledger.
static bool print_filter(void *userdata, const char *filter, const char *value, char *out, size_t size) {
  if (!strcmp(filter, "words")) { amount_in_words(value, out, size); return true; }
  static const char *cases[] = {"nom", "gen", "dat", "acc", "ins", "pre"};
  for (size_t c = 0; c < ARRAY_LEN(cases); c++) {
    if (strcmp(filter, cases[c])) continue;
    const db_setting_t *settings = first_record(g_ledger->cfg, TABLE_SETTINGS);
    ui_inflect_name(settings ? settings->language : NULL, value, (ui_case_t)c, out, size);
    return true;
  }
  return false;
}

static bool print_emit_row(print_ctx_t *ctx, int table, const void *record, bool owned,
                           script_template_emit_t emit, void *target) {
  if (ctx->count == ctx->capacity) {
    int capacity = ctx->capacity ? ctx->capacity * 2 : 32;
    print_row_t *rows = realloc(ctx->rows, capacity * sizeof(*rows));
    if (!rows) { if (owned) free((void *)record); return false; }
    ctx->rows = rows; ctx->capacity = capacity;
  }
  ctx->rows[ctx->count++] = (print_row_t){table, record, owned};
  emit(ctx->count, target);
  return true;
}

// The source maps template blocks to Ledger's tables. Tax grouping is a
// Ledger invoice rule; iteration and filtering are the shared Lua renderer's.
static bool print_rows(void *userdata, const char *name, int parent, script_template_emit_t emit, void *target) {
  print_ctx_t *ctx = userdata;
  bool tax = !strcmp(name, "tax_totals");
  const db_table_schema_t *table = db_schema_table_named(g_ledger->ib->schema, tax || !strcmp(name, "lines") ? "doc_lines" : name);
  if (!table) return false;
  if (tax && !ctx->doc) return true;
  int by_doc = db_schema_field_index(table, "document_id");
  result_node_t *rows = (result_node_t *)send_db_message(g_ledger->ib, dbFetch,
    MAKEDWORD(table->table_id, ctx->doc && by_doc >= 0 ? by_doc : 0), (void *)(intptr_t)(ctx->doc && by_doc >= 0 ? ctx->doc->id : 0));
  bool ok = true;
  for (result_node_t *node = rows; node && ok; node = node->next) {
    const void *record = *(void **)node->data;
    if (tax) {
      const db_doc_line_t *line = record;
      bool seen = false;
      for (result_node_t *before = rows; before != node; before = before->next)
        if ((*(db_doc_line_t **)before->data)->vat_rate_id == line->vat_rate_id) { seen = true; break; }
      if (seen) continue;
      db_doc_line_t *total = calloc(1, sizeof(*total));
      if (!total) { ok = false; break; }
      total->vat_rate_id = line->vat_rate_id;
      for (result_node_t *r = node; r; r = r->next) {
        const db_doc_line_t *item = *(db_doc_line_t **)r->data;
        if (item->vat_rate_id != line->vat_rate_id) continue;
        total->amount += item->amount; total->vat += item->vat; total->total += item->total;
      }
      record = total;
    }
    ok = print_emit_row(ctx, table->table_id, record, tax, emit, target);
  }
  free_result_list(rows);
  return ok;
}

static char *read_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *text = n >= 0 ? malloc((size_t)n + 1) : NULL;
  if (text && fread(text, 1, (size_t)n, f) != (size_t)n) { free(text); text = NULL; }
  if (text) text[n] = '\0';
  fclose(f);
  return text;
}

static void filename_part(const char *input, char *out, size_t size) {
  const unsigned char *p = (const unsigned char *)input;
  size_t n = 0;
  for (; *p && n + 4 < size; p++) {
    if (*p < 32 || strchr("/\\:<>\"|?*%", *p)) n += (size_t)snprintf(out + n, size - n, "%%%02X", *p);
    else out[n++] = (char)*p;
  }
  out[n] = '\0';
}

static bool write_form(const db_print_form_t *form, const db_document_t *doc, const char *dir,
                       char *report, size_t report_sz) {
  char src[LEDGER_PATH_MAX * 2], dst[LEDGER_PATH_MAX * 2], temp[LEDGER_PATH_MAX * 2 + 8], number[128];
  if (!form->template[0] || strpbrk(form->template, "/\\") || strstr(form->template, "..")) {
    fprintf(stderr, "[ledger] unsafe print template path: %s\n", form->template);
    return false;
  }
  snprintf(src, sizeof(src), "%s/print/%s", g_ledger->config_dir, form->template);
  filename_part(doc && doc->number[0] ? doc->number : "draft", number, sizeof(number));
  if (doc) snprintf(dst, sizeof(dst), "%s/%s-%s", dir, number, form->template);
  else snprintf(dst, sizeof(dst), "%s/%s", dir, form->template);
  snprintf(temp, sizeof(temp), "%s.tmp", dst);
  char *text = read_file(src);
  FILE *out = text ? fopen(temp, "wb") : NULL;
  if (!out) {
    fprintf(stderr, "[ledger] print form failed: template=%s output=%s\n", src, temp);
    free(text);
    return false;
  }
  print_ctx_t ctx = {.doc = doc};
  const script_template_source_t source = {&ctx, print_value, print_rows, print_filter};
  const char *ext = strrchr(form->template, '.');
  const char *format = ext && !strcmp(ext, ".csv") ? "csv" :
    ext && (!strcmp(ext, ".html") || !strcmp(ext, ".htm") || !strcmp(ext, ".xml")) ? "html" : "none";
  char *rendered = NULL, error[1024];
  bool failed = !script_template_render(text, format, &source, &rendered, error, sizeof(error));
  if (!failed) failed = fwrite(rendered, 1, strlen(rendered), out) != strlen(rendered);
  if (fclose(out)) failed = true;
  for (int i = 0; i < ctx.count; i++) if (ctx.rows[i].owned) free((void *)ctx.rows[i].record);
  free(ctx.rows); free(rendered); free(text);
  const char *extension = strrchr(form->template, '.');
  if (!failed && extension && !strcmp(extension, ".xml")) {
    xmlDocPtr xml = xmlReadFile(temp, NULL, XML_PARSE_NONET);
    if (xml) xmlFreeDoc(xml); else failed = true;
  }
  if (!failed && !file_replace(temp, dst)) failed = true;
  if (failed) {
    remove(temp);
    snprintf(report, report_sz, "%s", ui_tr("The print form could not be completed. No incomplete file was published."));
    return false;
  }
  size_t used = strlen(report);
  if (used < report_sz) snprintf(report + used, report_sz - used, "%s\n  %s\n", form->name, dst);
  return true;
}

// Writes the forms of the document's type (doc) or the report forms (NULL)
// into <data_dir>/print and lists the written files in report.
static int print_forms(const db_document_t *doc, int selected_form, char *report, size_t report_sz) {
  char dir[LEDGER_PATH_MAX + 8];
  snprintf(dir, sizeof(dir), "%s/print", g_ledger->data_dir);
  axMkDir(dir);
  report[0] = '\0';
  int written = 0;
  int field = ledger_field(g_ledger->cfg, TABLE_PRINT_FORMS, "doc_type_id");
  result_node_t *forms = (result_node_t *)send_db_message(g_ledger->cfg, dbFetch,
    MAKEDWORD(TABLE_PRINT_FORMS, field), (void *)(intptr_t)(doc ? doc->doc_type_id : 0));
  int form_count = count_result_list(forms), index = 0;
  bool *skip = form_count ? calloc((size_t)form_count, sizeof(*skip)) : NULL;
  if (form_count && !skip) { free_result_list(forms); return 0; }
  // Validate mandatory forms before replacing any output. Optional formats report why they were skipped.
  for (result_node_t *n = forms; n; n = n->next, index++) {
    const db_print_form_t *form = *(db_print_form_t **)n->data;
    if (selected_form && form->id != selected_form) { skip[index] = true; continue; }
    char error[512] = "";
    bool valid = true;
    if (doc && ((form->valid_from[0] && strcmp(doc->date, form->valid_from) < 0) ||
                (form->valid_to[0] && strcmp(doc->date, form->valid_to) > 0))) {
      snprintf(error, sizeof(error), "%s", ui_tr("No verified print form covers this document date."));
      valid = false;
    }
    const db_doc_type_t *type = doc ? (const db_doc_type_t *)send_db_message(g_ledger->cfg, dbFind, TABLE_DOC_TYPES, (void *)(intptr_t)doc->doc_type_id) : NULL;
    ledger_post_t p = { g_ledger->ib, g_ledger->cfg, (db_document_t *)doc, type, doc && doc->currency_id ? doc->rate : 1 };
    if (valid) valid = ledger_script_print(&p, form, error, sizeof(error));
    if (!valid) {
      fprintf(stderr, "[ledger] print rejected: form=%d: %s\n", form->id, error);
      if (!form->optional) {
        snprintf(report, report_sz, "%s", error);
        free(skip); free_result_list(forms);
        return 0;
      }
      skip[index] = true;
      size_t used = strlen(report);
      if (used < report_sz) snprintf(report + used, report_sz - used, "%s: %s\n", form->name, error);
    }
  }
  index = 0;
  for (result_node_t *n = forms; n; n = n->next, index++) {
    const db_print_form_t *form = *(db_print_form_t **)n->data;
    if (skip[index] || (!doc && !form->report[0])) continue;
    written += write_form(form, doc, dir, report, report_sz);
  }
  free(skip);
  free_result_list(forms);
  return written;
}

int ledger_print_form(int doc_id, int form_id, char *report, size_t report_sz) {
  if (report_sz) report[0] = '\0';
  const db_document_t *doc = (const db_document_t *)send_db_message(g_ledger->ib, dbFind, TABLE_DOCUMENTS, (void *)(intptr_t)doc_id);
  const db_print_form_t *form = (const db_print_form_t *)send_db_message(g_ledger->cfg, dbFind, TABLE_PRINT_FORMS, (void *)(intptr_t)form_id);
  if (!doc || !form || form->doc_type_id != doc->doc_type_id) return 0;
  return print_forms(doc, form_id, report, report_sz);
}

int ledger_print_document(int doc_id, char *report, size_t report_sz) {
  db_document_t *doc = (db_document_t *)send_db_message(g_ledger->ib, dbFind,
    MAKEDWORD(TABLE_DOCUMENTS, 0), (void *)(intptr_t)doc_id);
  if (report_sz) report[0] = '\0';
  if (!doc) return 0;
  return print_forms(doc, 0, report, report_sz);
}

int ledger_print_reports(char *report, size_t report_sz) {
  return print_forms(NULL, 0, report, report_sz);
}
