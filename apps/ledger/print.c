// Print forms: the configuration ships HTML templates for its legal forms
// (cfg.print_forms); this fills them from a document or from a report.
//
// Template syntax:
//   {{field}}              document field or join path: {{counterparty.name}}
//   {{org.field}}          organization details
//   {{cfg.field}}          configuration settings: {{cfg.currency}}
//   {{period.date_from}}   report settings
//   {{#table}}..{{/table}} repeated per row of an infobase table: the
//                          document's rows when the table has document_id
//                          ({{#lines}} is doc_lines), every row otherwise
//                          (report tables: {{#cash_book}}); {{n}} is the row number
//   {{field|dmy}}          ISO date as DD.MM.YYYY
//   {{field|words}}        amount in words in the configuration's language
//   {{field|dat}}          a person's name in a case: gen, dat, acc, ins, pre
//                          ("Выдать {{counterparty.name|dat}}"); organizations stay
#include "ledger.h"

#define PRINT_FIELD_MAX 512

typedef struct {
  FILE *out;
  const db_document_t *doc;  // NULL for report forms
  int row_table;             // the {{#table}} being repeated, or -1
  const void *row;
  int n;
} print_ctx_t;

static void put_escaped(FILE *out, const char *s) {
  for (; *s; s++) {
    switch (*s) {
      case '<': fputs("&lt;", out);   break;
      case '>': fputs("&gt;", out);   break;
      case '&': fputs("&amp;", out);  break;
      case '"': fputs("&quot;", out); break;
      default:  fputc(*s, out);       break;
    }
  }
}

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

static bool resolve(const print_ctx_t *ctx, const char *name, char *buf, size_t sz) {
  buf[0] = '\0';
  if (!strcmp(name, "n")) { snprintf(buf, sz, "%d", ctx->n); return true; }
  if (!strncmp(name, "org.", 4))
    return db_get_field_text(g_ledger->ib, TABLE_ORGANIZATIONS, first_record(g_ledger->ib, TABLE_ORGANIZATIONS), name + 4, buf, sz);
  if (!strncmp(name, "cfg.", 4))
    return db_get_field_text(g_ledger->cfg, TABLE_SETTINGS, first_record(g_ledger->cfg, TABLE_SETTINGS), name + 4, buf, sz);
  if (!strncmp(name, "period.", 7))
    return db_get_field_text(g_ledger->ib, TABLE_REPORT_SETTINGS, ledger_report_settings(g_ledger->ib, g_ledger->cfg), name + 7, buf, sz);
  if (ctx->row && table_has(g_ledger->ib, ctx->row_table, name))
    return db_get_field_text(g_ledger->ib, ctx->row_table, ctx->row, name, buf, sz);
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

static void put_tag(const print_ctx_t *ctx, const char *tag, size_t len) {
  char name[128], value[PRINT_FIELD_MAX];
  snprintf(name, sizeof(name), "%.*s", (int)len, tag);
  char *filter = strchr(name, '|');
  if (filter) *filter++ = '\0';
  if (!resolve(ctx, name, value, sizeof(value))) {
    fprintf(stderr, "[ledger] print template field not found: %s\n", name);
    return;
  }
  int y, m, d;
  if (filter && !strcmp(filter, "dmy") && sscanf(value, "%d-%d-%d", &y, &m, &d) == 3)
    snprintf(value, sizeof(value), "%02d.%02d.%04d", d, m, y);
  static const char *cases[] = { "nom", "gen", "dat", "acc", "ins", "pre" };
  for (size_t c = 0; filter && c < ARRAY_LEN(cases); c++) {
    if (strcmp(filter, cases[c])) continue;
    const db_setting_t *settings = first_record(g_ledger->cfg, TABLE_SETTINGS);
    char name[PRINT_FIELD_MAX];
    ui_inflect_name(settings ? settings->language : NULL, value, (ui_case_t)c, name, sizeof(name));
    snprintf(value, sizeof(value), "%s", name);
  }
  if (filter && !strcmp(filter, "words")) {
    char words[PRINT_FIELD_MAX];
    amount_in_words(value, words, sizeof(words));
    snprintf(value, sizeof(value), "%s", words);
  }
  put_escaped(ctx->out, value);
}

static void render(print_ctx_t *ctx, const char *s, const char *end);

// {{#table}}: the document's rows of a table with document_id, or all rows.
static const char *render_block(print_ctx_t *ctx, const char *open, const char *close, const char *end) {
  char name[64], stop_tag[80];
  snprintf(name, sizeof(name), "%.*s", (int)(close - open - 3), open + 3);
  snprintf(stop_tag, sizeof(stop_tag), "{{/%s}}", name);
  const char *body = close + 2, *stop = strstr(body, stop_tag);
  if (!stop || stop >= end) { fprintf(stderr, "[ledger] print template: unterminated {{#%s}}\n", name); return end; }
  const db_table_schema_t *table = db_schema_table_named(g_ledger->ib->schema, strcmp(name, "lines") ? name : "doc_lines");
  if (!table) { fprintf(stderr, "[ledger] print template: unknown table {{#%s}}\n", name); return stop + strlen(stop_tag); }
  int by_doc = db_schema_field_index(table, "document_id");
  result_node_t *rows = (result_node_t *)send_db_message(g_ledger->ib, dbFetch,
    MAKEDWORD(table->table_id, ctx->doc && by_doc >= 0 ? by_doc : 0), (void *)(intptr_t)(ctx->doc && by_doc >= 0 ? ctx->doc->id : 0));
  print_ctx_t inner = *ctx;
  inner.row_table = table->table_id;
  inner.n = 0;
  for (result_node_t *n = rows; n; n = n->next) {
    inner.row = *(void **)n->data;
    inner.n++;
    render(&inner, body, stop);
  }
  free_result_list(rows);
  return stop + strlen(stop_tag);
}

static void render(print_ctx_t *ctx, const char *s, const char *end) {
  while (s < end) {
    const char *open = strstr(s, "{{");
    if (!open || open >= end) { fwrite(s, 1, (size_t)(end - s), ctx->out); return; }
    fwrite(s, 1, (size_t)(open - s), ctx->out);
    const char *close = strstr(open, "}}");
    if (!close || close >= end) { fputs(open, ctx->out); return; }
    if (open[2] == '#') { s = render_block(ctx, open, close, end); continue; }
    put_tag(ctx, open + 2, (size_t)(close - open - 2));
    s = close + 2;
  }
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

static bool write_form(const db_print_form_t *form, const db_document_t *doc, const char *dir,
                       char *report, size_t report_sz) {
  char src[LEDGER_PATH_MAX * 2], dst[LEDGER_PATH_MAX * 2];
  snprintf(src, sizeof(src), "%s/print/%s", g_ledger->config_dir, form->template);
  if (doc) snprintf(dst, sizeof(dst), "%s/%s-%s", dir, doc->number[0] ? doc->number : "draft", form->template);
  else snprintf(dst, sizeof(dst), "%s/%s", dir, form->template);
  char *text = read_file(src);
  FILE *out = text ? fopen(dst, "wb") : NULL;
  if (!out) {
    fprintf(stderr, "[ledger] print form failed: template=%s output=%s\n", src, dst);
    free(text);
    return false;
  }
  print_ctx_t ctx = { .out = out, .doc = doc, .row_table = -1 };
  render(&ctx, text, text + strlen(text));
  fclose(out);
  free(text);
  size_t used = strlen(report);
  snprintf(report + used, report_sz - used, "%s\n  %s\n", form->name, dst);
  return true;
}

// Writes the forms of the document's type (doc) or the report forms (NULL)
// into <data_dir>/print and lists the written files in report.
static int print_forms(const db_document_t *doc, char *report, size_t report_sz) {
  char dir[LEDGER_PATH_MAX + 8];
  snprintf(dir, sizeof(dir), "%s/print", g_ledger->data_dir);
  axMkDir(dir);
  report[0] = '\0';
  int written = 0;
  int field = ledger_field(g_ledger->cfg, TABLE_PRINT_FORMS, "doc_type_id");
  result_node_t *forms = (result_node_t *)send_db_message(g_ledger->cfg, dbFetch,
    MAKEDWORD(TABLE_PRINT_FORMS, field), (void *)(intptr_t)(doc ? doc->doc_type_id : 0));
  for (result_node_t *n = forms; n; n = n->next) {
    const db_print_form_t *form = *(db_print_form_t **)n->data;
    if (!doc && !form->report[0]) continue;
    written += write_form(form, doc, dir, report, report_sz);
  }
  free_result_list(forms);
  return written;
}

int ledger_print_document(int doc_id, char *report, size_t report_sz) {
  db_document_t *doc = (db_document_t *)send_db_message(g_ledger->ib, dbFind,
    MAKEDWORD(TABLE_DOCUMENTS, 0), (void *)(intptr_t)doc_id);
  if (!doc) return 0;
  return print_forms(doc, report, report_sz);
}

int ledger_print_reports(char *report, size_t report_sz) {
  return print_forms(NULL, report, report_sz);
}
