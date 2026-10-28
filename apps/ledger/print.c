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
//   {{#table field=value}} only the rows whose field (or join path) shows
//                          value: {{#vat_book book=1}}, {{#vat_summary vat_rate.rate=19}}
//   {{#tax_totals}}       invoice lines grouped by tax code, in document currency
//   {{field|dmy}}          ISO date as DD.MM.YYYY; |ymd as YYYYMMDD, |ddmm as DDMM
//   {{field|dot}}          a number as 1234.50, |comma as 1234,50 (exports)
//   {{field|words}}        amount in words in the configuration's language
//   {{field|dat}}          a person's name in a case: gen, dat, acc, ins, pre
//                          ("Выдать {{counterparty.name|dat}}"); organizations stay
//
// A template's extension picks the escaping: .html escapes markup, .csv
// doubles quotes inside quoted fields, anything else writes values as they are.
#include "ledger.h"
#include <libxml/parser.h>
#include <orion/user/file.h>

#define PRINT_FIELD_MAX 512

typedef enum { ESCAPE_HTML, ESCAPE_CSV, ESCAPE_NONE } escape_t;

typedef struct {
  FILE *out;
  escape_t escape;
  const db_document_t *doc;  // NULL for report forms
  int row_table;             // the {{#table}} being repeated, or -1
  const void *row;
  int n;
  bool *failed;
} print_ctx_t;

static void put_escaped(const print_ctx_t *ctx, const char *s) {
  FILE *out = ctx->out;
  for (; *s; s++) {
    if (ctx->escape == ESCAPE_CSV) { if (*s == '"') fputc('"', out); fputc(*s, out); continue; }
    if (ctx->escape == ESCAPE_NONE) { fputc(*s, out); continue; }
    switch (*s) {
      case '<': fputs("&lt;", out);   break;
      case '>': fputs("&gt;", out);   break;
      case '&': fputs("&amp;", out);  break;
      case '"': fputs("&quot;", out); break;
      default:  fputc(*s, out);       break;
    }
  }
}

static escape_t escape_for(const char *template) {
  const char *ext = strrchr(template, '.');
  if (ext && !strcmp(ext, ".csv")) return ESCAPE_CSV;
  if (ext && (!strcmp(ext, ".html") || !strcmp(ext, ".htm") || !strcmp(ext, ".xml"))) return ESCAPE_HTML;
  return ESCAPE_NONE;
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
    *ctx->failed = true;
    return;
  }
  if (filter && !strcmp(filter, "dash") && !value[0]) snprintf(value, sizeof(value), "—");
  if (filter && !strcmp(filter, "zero_dash") && ui_parse_number(value) == 0) snprintf(value, sizeof(value), "—");
  // Dates come as stored (2026-03-10) or as the catalog shows them (10.03.2026).
  int y = 0, m = 0, d = 0;
  bool date = filter && (sscanf(value, "%d-%d-%d", &y, &m, &d) == 3 || sscanf(value, "%d.%d.%d", &d, &m, &y) == 3) && y > 31;
  if (date && !strcmp(filter, "iso")) snprintf(value, sizeof(value), "%04d-%02d-%02d", y, m, d);
  if (filter && (!strcmp(filter, "decimal3") || !strcmp(filter, "decimal3comma") ||
                 !strcmp(filter, "zero_comma") || !strcmp(filter, "zero_decimal3comma"))) {
    double numeric = ui_parse_number(value);
    bool zero_dash = !strncmp(filter, "zero_", 5) && numeric == 0;
    if (zero_dash) snprintf(value, sizeof(value), "—");
    else {
      snprintf(value, sizeof(value), strstr(filter, "decimal3") ? "%.3f" : "%.2f", numeric);
      if (strstr(filter, "comma")) for (char *c = value; *c; c++) if (*c == '.') *c = ',';
    }
  }
  if (date && !strcmp(filter, "dmy"))  snprintf(value, sizeof(value), "%02d.%02d.%04d", d, m, y);
  if (date && !strcmp(filter, "ymd"))  snprintf(value, sizeof(value), "%04d%02d%02d", y, m, d);
  if (date && !strcmp(filter, "ddmm")) snprintf(value, sizeof(value), "%02d%02d", d, m);
  if (filter && (!strcmp(filter, "dot") || !strcmp(filter, "comma"))) {  // plain decimals for exports
    snprintf(value, sizeof(value), "%.2f", ui_parse_number(value));
    if (*filter == 'c') for (char *c = value; *c; c++) if (*c == '.') *c = ',';
  }
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
  put_escaped(ctx, value);
}

static void render(print_ctx_t *ctx, const char *s, const char *end);

// Invoice tax totals use the invoice's own lines, including unsaved drafts.
static void render_tax_totals(print_ctx_t *ctx, const char *body, const char *stop) {
  if (!ctx->doc) return;
  result_node_t *rows = (result_node_t *)send_db_message(g_ledger->ib, dbFetch,
    MAKEDWORD(TABLE_DOC_LINES, ledger_field(g_ledger->ib, TABLE_DOC_LINES, "document_id")), (void *)(intptr_t)ctx->doc->id);
  print_ctx_t inner = *ctx;
  inner.row_table = TABLE_DOC_LINES;
  inner.n = 0;
  for (result_node_t *n = rows; n; n = n->next) {
    const db_doc_line_t *line = *(db_doc_line_t **)n->data;
    bool seen = false;
    for (result_node_t *before = rows; before != n; before = before->next)
      if ((*(db_doc_line_t **)before->data)->vat_rate_id == line->vat_rate_id) { seen = true; break; }
    if (seen) continue;
    db_doc_line_t total = { .vat_rate_id = line->vat_rate_id };
    for (result_node_t *r = n; r; r = r->next) {
      const db_doc_line_t *row = *(db_doc_line_t **)r->data;
      if (row->vat_rate_id != line->vat_rate_id) continue;
      total.amount += row->amount; total.vat += row->vat; total.total += row->total;
    }
    inner.row = &total;
    inner.n++;
    render(&inner, body, stop);
  }
  free_result_list(rows);
}


// {{#table}}: the document's rows of a table with document_id, or all rows;
// {{#table field=value}} keeps the rows whose field reads value.
static const char *render_block(print_ctx_t *ctx, const char *open, const char *close, const char *end) {
  char name[160], stop_tag[168];
  struct { char *field, *value; bool negate; } filters[4];
  int nfilters = 0;
  snprintf(name, sizeof(name), "%.*s", (int)(close - open - 3), open + 3);
  char *cursor = strchr(name, ' ');  // name field=value field=value
  if (cursor) *cursor++ = '\0';
  while (cursor && nfilters < (int)ARRAY_LEN(filters)) {
    char *next = strchr(cursor, ' ');
    if (next) *next++ = '\0';
    char *eq = strchr(cursor, '=');
    if (eq) {
      filters[nfilters].negate = eq > cursor && eq[-1] == '!';
      if (filters[nfilters].negate) eq[-1] = '\0';
      *eq = '\0'; filters[nfilters].field = cursor; filters[nfilters++].value = eq + 1;
    }
    cursor = next;
  }
  snprintf(stop_tag, sizeof(stop_tag), "{{/%s}}", name);
  const char *body = close + 2, *stop = strstr(body, stop_tag);
  if (!stop || stop >= end) { fprintf(stderr, "[ledger] print template: unterminated {{#%s}}\n", name); *ctx->failed = true; return end; }
  if (!strcmp(name, "if")) {
    bool keep = nfilters > 0;
    for (int f = 0; f < nfilters && keep; f++) {
      char value[PRINT_FIELD_MAX];
      if (!resolve(ctx, filters[f].field, value, sizeof(value))) { *ctx->failed = true; keep = false; }
      else keep = (!strcmp(value, filters[f].value)) != filters[f].negate;
    }
    if (keep) render(ctx, body, stop);
    return stop + strlen(stop_tag);
  }
  if (!strcmp(name, "tax_totals")) {
    render_tax_totals(ctx, body, stop);
    return stop + strlen(stop_tag);
  }
  const db_table_schema_t *table = db_schema_table_named(g_ledger->ib->schema, strcmp(name, "lines") ? name : "doc_lines");
  if (!table) { fprintf(stderr, "[ledger] print template: unknown table {{#%s}}\n", name); *ctx->failed = true; return stop + strlen(stop_tag); }
  int by_doc = db_schema_field_index(table, "document_id");
  result_node_t *rows = (result_node_t *)send_db_message(g_ledger->ib, dbFetch,
    MAKEDWORD(table->table_id, ctx->doc && by_doc >= 0 ? by_doc : 0), (void *)(intptr_t)(ctx->doc && by_doc >= 0 ? ctx->doc->id : 0));
  print_ctx_t inner = *ctx;
  inner.row_table = table->table_id;
  inner.n = 0;
  for (result_node_t *n = rows; n; n = n->next) {
    inner.row = *(void **)n->data;
    bool kept = true;
    for (int f = 0; f < nfilters && kept; f++) {
      char text[PRINT_FIELD_MAX] = "";
      const char *value = filters[f].value;
      db_get_field_text(g_ledger->ib, table->table_id, inner.row, filters[f].field, text, sizeof(text));
      bool number = value[0] && strspn(value, "0123456789.,-") == strlen(value);
      kept = (number ? ui_parse_number(text) == ui_parse_number(value) : !strcmp(text, value)) != filters[f].negate;
    }
    if (!kept) continue;
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
    if (!close || close >= end) { *ctx->failed = true; return; }
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
  bool failed = false;
  print_ctx_t ctx = { .out = out, .escape = escape_for(form->template), .doc = doc, .row_table = -1, .failed = &failed };
  render(&ctx, text, text + strlen(text));
  failed = ferror(out) || failed;
  if (fclose(out)) failed = true;
  free(text);
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
