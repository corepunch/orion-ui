// Print forms: the configuration ships HTML templates for its legal forms
// (cfg.print_forms); this fills them from a document.
//
// Template syntax:
//   {{field}}              document field or join path: {{counterparty.name}}
//   {{org.field}}          organization details
//   {{cfg.field}}          configuration settings: {{cfg.currency}}
//   {{#lines}}..{{/lines}} repeated per document line; {{n}} is the row number
//   {{field|dmy}}          ISO date as DD.MM.YYYY

#include "ledger.h"

#define PRINT_FIELD_MAX 512

typedef struct {
  FILE *out;
  const db_document_t *doc;
  const db_doc_line_t *line;
  int row;
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

static bool resolve(const print_ctx_t *ctx, const char *name, char *buf, size_t sz) {
  buf[0] = '\0';
  if (!strcmp(name, "n")) { snprintf(buf, sz, "%d", ctx->row); return true; }
  if (!strncmp(name, "org.", 4))
    return db_get_field_text(g_ledger->ib, TABLE_ORGANIZATIONS, first_record(g_ledger->ib, TABLE_ORGANIZATIONS), name + 4, buf, sz);
  if (!strncmp(name, "cfg.", 4))
    return db_get_field_text(g_ledger->cfg, TABLE_SETTINGS, first_record(g_ledger->cfg, TABLE_SETTINGS), name + 4, buf, sz);
  if (ctx->line && db_get_field_text(g_ledger->ib, TABLE_DOC_LINES, ctx->line, name, buf, sz)) return true;
  return db_get_field_text(g_ledger->ib, TABLE_DOCUMENTS, ctx->doc, name, buf, sz);
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
  put_escaped(ctx->out, value);
}

static void render(print_ctx_t *ctx, const char *s, const char *end) {
  while (s < end) {
    const char *open = strstr(s, "{{");
    if (!open || open >= end) { fwrite(s, 1, (size_t)(end - s), ctx->out); return; }
    fwrite(s, 1, (size_t)(open - s), ctx->out);
    const char *close = strstr(open, "}}");
    if (!close || close >= end) { fputs(open, ctx->out); return; }
    if (!strncmp(open, "{{#lines}}", 10)) {
      const char *body = open + 10, *stop = strstr(body, "{{/lines}}");
      if (!stop || stop >= end) { fprintf(stderr, "[ledger] print template: unterminated {{#lines}}\n"); return; }
      int field = ledger_field(g_ledger->ib, TABLE_DOC_LINES, "document_id");
      result_node_t *lines = (result_node_t *)send_db_message(g_ledger->ib, dbFetch,
        MAKEDWORD(TABLE_DOC_LINES, field), (void *)(intptr_t)ctx->doc->id);
      ctx->row = 0;
      for (result_node_t *n = lines; n; n = n->next) {
        ctx->line = *(db_doc_line_t **)n->data;
        ctx->row++;
        render(ctx, body, stop);
      }
      ctx->line = NULL;
      free_result_list(lines);
      s = stop + 10;
      continue;
    }
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

// Writes every print form of the document's type into <data_dir>/print and
// lists the written files in report. Returns the number of forms written.
int ledger_print_document(int doc_id, char *report, size_t report_sz) {
  db_document_t *doc = (db_document_t *)send_db_message(g_ledger->ib, dbFind,
    MAKEDWORD(TABLE_DOCUMENTS, 0), (void *)(intptr_t)doc_id);
  if (!doc) return 0;
  char dir[LEDGER_PATH_MAX + 8];
  snprintf(dir, sizeof(dir), "%s/print", g_ledger->data_dir);
  axMkDir(dir);
  report[0] = '\0';
  int written = 0;
  int field = ledger_field(g_ledger->cfg, TABLE_PRINT_FORMS, "doc_type_id");
  result_node_t *forms = (result_node_t *)send_db_message(g_ledger->cfg, dbFetch,
    MAKEDWORD(TABLE_PRINT_FORMS, field), (void *)(intptr_t)doc->doc_type_id);
  for (result_node_t *n = forms; n; n = n->next) {
    const db_print_form_t *form = *(db_print_form_t **)n->data;
    char src[LEDGER_PATH_MAX * 2], dst[LEDGER_PATH_MAX * 2];
    snprintf(src, sizeof(src), "%s/print/%s", g_ledger->config_dir, form->template);
    snprintf(dst, sizeof(dst), "%s/%s-%s", dir, doc->number[0] ? doc->number : "draft", form->template);
    char *text = read_file(src);
    FILE *out = text ? fopen(dst, "wb") : NULL;
    if (!out) {
      fprintf(stderr, "[ledger] print form failed: template=%s output=%s\n", src, dst);
      free(text);
      continue;
    }
    print_ctx_t ctx = { .out = out, .doc = doc };
    render(&ctx, text, text + strlen(text));
    fclose(out);
    free(text);
    size_t used = strlen(report);
    snprintf(report + used, report_sz - used, "%s\n  %s\n", form->name, dst);
    written++;
  }
  free_result_list(forms);
  return written;
}
