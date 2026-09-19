#include "formeditor.h"
#include <orion/commctl/commctl.h>
#include <ctype.h>
#include <libxml/tree.h>
#include <strings.h>

#define FE_RUNTIME_MAX_CONTROLS 512
#define FE_RUNTIME_MAX_ALLOCS 4096

typedef struct {
  tableview_params_t table;
  const char *field_names[FE_MAX_TABLE_COLUMNS + 1];
  const char *titles[FE_MAX_TABLE_COLUMNS + 1];
  int widths[FE_MAX_TABLE_COLUMNS + 1];
  int min_widths[FE_MAX_TABLE_COLUMNS + 1];
  combobox_params_t combo;
} runtime_params_t;

typedef struct {
  runtime_params_t params[FE_RUNTIME_MAX_CONTROLS];
  int param_count;
  int next_generated_id;
  void *allocs[FE_RUNTIME_MAX_ALLOCS];
  int alloc_count;
} runtime_build_ctx_t;

static void *runtime_alloc(runtime_build_ctx_t *ctx, size_t size) {
  if (!ctx || size == 0 || ctx->alloc_count >= FE_RUNTIME_MAX_ALLOCS)
    return NULL;
  void *p = calloc(1, size);
  if (!p)
    return NULL;
  ctx->allocs[ctx->alloc_count++] = p;
  return p;
}

static char *runtime_strdup(runtime_build_ctx_t *ctx, const char *s) {
  if (!s)
    return NULL;
  size_t n = strlen(s) + 1;
  char *d = (char *)runtime_alloc(ctx, n);
  if (!d)
    return NULL;
  memcpy(d, s, n);
  return d;
}

static void runtime_release(runtime_build_ctx_t *ctx) {
  if (!ctx)
    return;
  for (int i = ctx->alloc_count - 1; i >= 0; i--)
    free(ctx->allocs[i]);
  ctx->alloc_count = 0;
  ctx->param_count = 0;
}

static int runtime_xml_attr_int(xmlNodePtr node, const char *name, int fallback) {
  xmlChar *v = xmlGetProp(node, BAD_CAST name);
  if (!v)
    return fallback;
  const char *s = (const char *)v;
  while (*s && isspace((unsigned char)*s))
    s++;
  char *end = NULL;
  long n = strtol(s, &end, 0);
  while (end && *end && isspace((unsigned char)*end))
    end++;
  bool ok = (end && *end == '\0');
  xmlFree(v);
  if (!ok)
    return fallback;
  return (int)n;
}

static char *runtime_xml_attr_dup(runtime_build_ctx_t *ctx, xmlNodePtr node, const char *name) {
  xmlChar *v = xmlGetProp(node, BAD_CAST name);
  if (!v)
    return NULL;
  char *out = runtime_strdup(ctx, (const char *)v);
  xmlFree(v);
  return out;
}

static void runtime_xml_attr_copy(xmlNodePtr node, const char *name, char *dst, size_t dst_sz) {
  if (!dst || dst_sz == 0)
    return;
  dst[0] = '\0';
  xmlChar *v = xmlGetProp(node, BAD_CAST name);
  if (!v)
    return;
  snprintf(dst, dst_sz, "%s", (const char *)v);
  xmlFree(v);
}

static void parse_rect_attr(irect16_t *out, const char *s) {
  if (!out) return;
  *out = (irect16_t){0, 0, 0, 0};
  if (!s || !*s) return;
  int a = 0, b = 0, c = 0, d = 0;
  int n = sscanf(s, "%d %d %d %d", &a, &b, &c, &d);
  if (n == 1) {
    *out = (irect16_t){(int16_t)a, (int16_t)a, (int16_t)a, (int16_t)a};
  } else if (n == 2) {
    *out = (irect16_t){(int16_t)a, (int16_t)b, (int16_t)a, (int16_t)b};
  } else if (n == 4) {
    *out = (irect16_t){(int16_t)a, (int16_t)b, (int16_t)c, (int16_t)d};
  }
}

static database_t *runtime_current_database(void) {
  database_t *db = ui_get_database();
  if (!db)
    db = get_database_by_name("db");
  return db;
}

static database_t *runtime_database_for_source(const char *source) {
  if (source && *source) {
    const char *dot = strchr(source, '.');
    if (dot) {
      char name[64];
      snprintf(name, sizeof(name), "%.*s", (int)(dot - source), source);
      for (int i = 0; g_app && i < g_app->project.database_count; i++) {
        database_t *db = g_app->project.databases[i];
        if (db && db->name && strcmp(db->name, name) == 0) return db;
      }
      database_t *db = get_database_by_name(name);
      if (db) return db;
    }
  }
  return runtime_current_database();
}

static const db_schema_def_t *runtime_schema(database_t *db) {
  if (!db)
    return NULL;
  return (const db_schema_def_t *)send_db_message(db, dbGetSchema, 0, NULL);
}

static const db_table_schema_t *runtime_table_by_name(const db_schema_def_t *schema,
                                                       const char *name) {
  if (!schema || !name || !*name) return NULL;
  for (int i = 0; i < schema->table_count; i++)
    if (schema->tables[i].name && strcmp(schema->tables[i].name, name) == 0)
      return &schema->tables[i];
  return NULL;
}

static bool runtime_resolve_table_source(database_t *db, const char *source,
                                         const db_table_schema_t **table_out,
                                         const db_field_schema_t **filter_field_out) {
  if (table_out)
    *table_out = NULL;
  if (filter_field_out)
    *filter_field_out = NULL;
  if (!source || !*source)
    return false;

  const db_schema_def_t *schema = runtime_schema(db);
  if (!schema)
    return false;

  char path[256];
  snprintf(path, sizeof(path), "%s", source);
  char *parts[4] = {0};
  int n = 0;
  for (char *p = strtok(path, "."); p && n < 4; p = strtok(NULL, "."))
    parts[n++] = p;

  if (n == 1 || n == 2) {
    const char *table_name = parts[n - 1];
    const db_table_schema_t *table = runtime_table_by_name(schema, table_name);
    if (!table)
      return false;
    if (table_out)
      *table_out = table;
    return true;
  }

  if (n == 3) {
    const db_table_schema_t *master = runtime_table_by_name(schema, parts[1]);
    const db_table_schema_t *detail = NULL;
    if (!master) return false;
    for (int i = 0; i < master->join_count; i++) {
      const db_join_schema_t *join = &master->joins[i];
      if (join->name && strcmp(join->name, parts[2]) == 0) {
        detail = runtime_table_by_name(schema, join->foreign_table);
        break;
      }
    }
    if (!detail) detail = runtime_table_by_name(schema, parts[2]);
    if (!detail) return false;
    if (table_out)
      *table_out = detail;
    return true;
  }

  return false;
}

static int runtime_table_id_for_source(const char *source) {
  const db_table_schema_t *table = NULL;
  if (!runtime_resolve_table_source(runtime_database_for_source(source), source,
                                    &table, NULL) || !table)
    return -1;
  return (int)table->table_id;
}

static runtime_params_t *runtime_next_params(runtime_build_ctx_t *ctx) {
  if (!ctx || ctx->param_count >= FE_RUNTIME_MAX_CONTROLS)
    return NULL;
  runtime_params_t *p = &ctx->params[ctx->param_count++];
  memset(p, 0, sizeof(*p));
  return p;
}

static bool str_ieq(const char *a, const char *b) {
  if (!a || !b)
    return false;
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
      return false;
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

static flags_t runtime_flag_from_name(const char *name) {
  if (!name || !*name) return 0;
  if (strncasecmp(name, "CONTROL_SIZE_", 13) == 0) {
    name += 13;
    if (str_ieq(name, "small")) return CONTROL_SIZE_SMALL;
    if (str_ieq(name, "mini")) return CONTROL_SIZE_MINI;
    if (str_ieq(name, "large")) return CONTROL_SIZE_LARGE;
    if (str_ieq(name, "regular")) return CONTROL_SIZE_REGULAR;
  }
  if (strncasecmp(name, "WINDOW_", 7) == 0) name += 7;
  else if (strncasecmp(name, "BUTTON_", 7) == 0) name += 7;
  if (str_ieq(name, "autolayout")) return WINDOW_AUTO_LAYOUT;
  if (str_ieq(name, "auto_layout")) return WINDOW_AUTO_LAYOUT;
  if (str_ieq(name, "stackh")) return WINDOW_STACK_HORIZONTAL;
  if (str_ieq(name, "stack_horizontal")) return WINDOW_STACK_HORIZONTAL;
  if (str_ieq(name, "stackv")) return WINDOW_STACK_VERTICAL;
  if (str_ieq(name, "stack_vertical")) return WINDOW_STACK_VERTICAL;
  if (str_ieq(name, "flexspace")) return WINDOW_FLEXSPACE;
  if (str_ieq(name, "vscroll")) return WINDOW_VSCROLL;
  if (str_ieq(name, "hscroll")) return WINDOW_HSCROLL;
  if (str_ieq(name, "notitle")) return WINDOW_NOTITLE;
  if (str_ieq(name, "nofill")) return WINDOW_NOFILL;
  if (str_ieq(name, "dialog")) return WINDOW_DIALOG;
  if (str_ieq(name, "toolbar")) return WINDOW_TOOLBAR;
  if (str_ieq(name, "statusbar")) return WINDOW_STATUSBAR;
  if (str_ieq(name, "noresize")) return WINDOW_NORESIZE;
  if (str_ieq(name, "notraybutton")) return WINDOW_NOTRAYBUTTON;
  if (str_ieq(name, "notabstop")) return WINDOW_NOTABSTOP;
  if (str_ieq(name, "alwaysontop")) return WINDOW_ALWAYSONTOP;
  if (str_ieq(name, "alwaysinback")) return WINDOW_ALWAYSINBACK;
  if (str_ieq(name, "hidden")) return WINDOW_HIDDEN;
  if (str_ieq(name, "noactivate")) return WINDOW_NOACTIVATE;
  if (str_ieq(name, "transparent")) return WINDOW_TRANSPARENT;
  if (str_ieq(name, "layout_container")) return WINDOW_LAYOUT_CONTAINER;
  if (str_ieq(name, "default")) return BUTTON_DEFAULT;
  if (str_ieq(name, "autoradio")) return BUTTON_AUTORADIO;
  if (str_ieq(name, "pushlike")) return BUTTON_PUSHLIKE;
  if (str_ieq(name, "control_small")) return CONTROL_SIZE_SMALL;
  if (str_ieq(name, "control_mini")) return CONTROL_SIZE_MINI;
  if (str_ieq(name, "control_large")) return CONTROL_SIZE_LARGE;
  return 0;
}

static flags_t runtime_parse_flags(const char *expr) {
  if (!expr || !*expr)
    return 0;

  char *end = NULL;
  long numeric = strtol(expr, &end, 0);
  if (end && *end == '\0')
    return (flags_t)numeric;

  flags_t out = 0;
  const char *p = expr;
  while (*p) {
    while (*p && (isspace((unsigned char)*p) || *p == ',' || *p == '|'))
      p++;
    if (!*p)
      break;

    char tok[96];
    int n = 0;
    while (*p && *p != ',' && *p != '|') {
      if (n < (int)sizeof(tok) - 1)
        tok[n++] = *p;
      p++;
    }
    tok[n] = '\0';

    while (n > 0 && isspace((unsigned char)tok[n - 1]))
      tok[--n] = '\0';
    int i = 0;
    while (tok[i] && isspace((unsigned char)tok[i]))
      i++;
    if (tok[i])
      out |= runtime_flag_from_name(tok + i);
  }

  return out;
}

static uint8_t runtime_parse_align_h(const char *s) {
  if (!s || !*s) return LAYOUT_ALIGN_STRETCH;
  if (str_ieq(s, "left") || str_ieq(s, "start")) return LAYOUT_ALIGN_START;
  if (str_ieq(s, "center")) return LAYOUT_ALIGN_CENTER;
  if (str_ieq(s, "right") || str_ieq(s, "end")) return LAYOUT_ALIGN_END;
  return LAYOUT_ALIGN_STRETCH;
}

static uint8_t runtime_parse_align_v(const char *s) {
  if (!s || !*s) return LAYOUT_ALIGN_STRETCH;
  if (str_ieq(s, "top") || str_ieq(s, "start")) return LAYOUT_ALIGN_START;
  if (str_ieq(s, "center")) return LAYOUT_ALIGN_CENTER;
  if (str_ieq(s, "bottom") || str_ieq(s, "end")) return LAYOUT_ALIGN_END;
  return LAYOUT_ALIGN_STRETCH;
}

static uint8_t runtime_parse_font(const char *s, bool *set) {
  if (set) *set = false;
  if (!s || !*s)
    return FONT_SMALL;
  if (set) *set = true;
  if (str_ieq(s, "system")) return FONT_SYSTEM;
  if (str_ieq(s, "smallest")) return FONT_SMALLEST;
  return FONT_SMALL;
}

static uint8_t runtime_parse_color(const char *s, bool *set) {
  if (set) *set = false;
  if (!s || !*s)
    return brTransparent;
  if (set) *set = true;
  if (str_ieq(s, "text-normal")) return brTextNormal;
  if (str_ieq(s, "text-disabled")) return brTextDisabled;
  if (str_ieq(s, "text-error")) return brTextError;
  if (str_ieq(s, "text-success")) return brTextSuccess;
  if (str_ieq(s, "control-bg")) return brControlBg;
  if (str_ieq(s, "workspace-bg")) return brWorkspaceBg;
  if (str_ieq(s, "panel-dark")) return brPanelDark;
  if (str_ieq(s, "panel-darker")) return brPanelDarker;
  return brTransparent;
}

static void runtime_fill_table_params(runtime_build_ctx_t *ctx, xmlNodePtr node,
                                      const void **out_lparam) {
  runtime_params_t *rp = runtime_next_params(ctx);
  if (!rp || !out_lparam)
    return;

  char source[128] = {0};
  runtime_xml_attr_copy(node, "source", source, sizeof(source));
  if (!source[0])
    runtime_xml_attr_copy(node, "db_source", source, sizeof(source));

  const db_table_schema_t *source_table = NULL;
  const db_field_schema_t *filter_field = NULL;
  rp->table.db = runtime_database_for_source(source);
  runtime_resolve_table_source(rp->table.db, source, &source_table, &filter_field);

  rp->table.table_id = source_table ? (int)source_table->table_id : -1;
  rp->table.filter_field = 0;
  char *cell_style = runtime_xml_attr_dup(ctx, node, "cell-style");
  if (cell_style && str_ieq(cell_style, "two-line"))
    rp->table.cell_style = REPORTVIEW_CELL_TWO_LINE;
  free(cell_style);

  int col = 0;
  for (xmlNodePtr c = node->children; c && col < FE_MAX_TABLE_COLUMNS; c = c->next) {
    if (c->type != XML_ELEMENT_NODE)
      continue;
    if (xmlStrcasecmp(c->name, BAD_CAST "Column") != 0)
      continue;

    char *field = runtime_xml_attr_dup(ctx, c, "field");
    char *title = runtime_xml_attr_dup(ctx, c, "title");
    int width = 80;
    xmlChar *width_x = xmlGetProp(c, BAD_CAST "width");
    if (width_x) {
      const char *s = (const char *)width_x;
      while (*s && isspace((unsigned char)*s))
        s++;
      char *end = NULL;
      long parsed = strtol(s, &end, 10);
      while (end && *end && isspace((unsigned char)*end))
        end++;
      if (end && *end == '\0' && parsed >= 0 && parsed <= INT32_MAX)
        width = (int)parsed;
      xmlFree(width_x);
    }
    int min_width = 0;
    xmlChar *min_width_x = xmlGetProp(c, BAD_CAST "min-width");
    if (min_width_x) {
      const char *s = (const char *)min_width_x;
      while (*s && isspace((unsigned char)*s))
        s++;
      char *end = NULL;
      long parsed = strtol(s, &end, 10);
      while (end && *end && isspace((unsigned char)*end))
        end++;
      if (end && *end == '\0' && parsed >= 0 && parsed <= INT32_MAX)
        min_width = (int)parsed;
      xmlFree(min_width_x);
    }

    if (!field)
      field = runtime_strdup(ctx, "id");
    if (!title)
      title = field;

    rp->field_names[col] = field;
    rp->titles[col] = title;
    rp->widths[col] = width;
    rp->min_widths[col] = min_width;
    col++;
  }

  if (col == 0) {
    rp->field_names[0] = runtime_strdup(ctx, "id");
    rp->titles[0] = runtime_strdup(ctx, "ID");
    rp->widths[0] = 80;
    rp->min_widths[0] = 0;
    col = 1;
  }

  rp->field_names[col] = NULL;
  rp->titles[col] = NULL;
  rp->table.field_names = rp->field_names;
  rp->table.column_titles = rp->titles;
  rp->table.column_widths = rp->widths;
  rp->table.column_min_widths = rp->min_widths;
  *out_lparam = &rp->table;
}

static void runtime_fill_combo_params(runtime_build_ctx_t *ctx, xmlNodePtr node,
                                      const void **out_lparam) {
  runtime_params_t *rp = runtime_next_params(ctx);
  if (!rp || !out_lparam)
    return;

  char source[128] = {0};
  runtime_xml_attr_copy(node, "source", source, sizeof(source));
  if (!source[0])
    runtime_xml_attr_copy(node, "db_source", source, sizeof(source));

  char *display = runtime_xml_attr_dup(ctx, node, "display");
  if (!display)
    display = runtime_xml_attr_dup(ctx, node, "db_display");
  char *value = runtime_xml_attr_dup(ctx, node, "value");
  if (!value)
    value = runtime_xml_attr_dup(ctx, node, "db_value");

  rp->combo.db = runtime_database_for_source(source);
  rp->combo.table_id = runtime_table_id_for_source(source);
  rp->combo.display_field = display ? display : "";
  rp->combo.value_field = value ? value : "";
  *out_lparam = &rp->combo;
}

static bool runtime_is_control_node(xmlNodePtr parent, xmlNodePtr node) {
  if (!node || node->type != XML_ELEMENT_NODE)
    return false;
  if (xmlStrcasecmp(node->name, BAD_CAST "Toolbar") == 0)
    return false;
  if (parent && xmlStrcasecmp(node->name, BAD_CAST "Column") == 0 &&
      (xmlStrcasecmp(parent->name, BAD_CAST "TableView") == 0 ||
       xmlStrcasecmp(parent->name, BAD_CAST "ReportView") == 0))
    return false;
  return true;
}

static bool runtime_is_report_column_node(xmlNodePtr node) {
  if (!node || xmlStrcasecmp(node->name, BAD_CAST "Column") != 0)
    return false;
  xmlNodePtr parent = node->parent;
  return parent &&
         (xmlStrcasecmp(parent->name, BAD_CAST "TableView") == 0 ||
          xmlStrcasecmp(parent->name, BAD_CAST "ReportView") == 0);
}

static const char *runtime_map_class_name(xmlNodePtr node) {
  const char *xml_name = node ? (const char *)node->name : NULL;
  if (!xml_name || !*xml_name)
    return "";
  if (runtime_is_report_column_node(node))
    return "ReportColumn";
  if (str_ieq(xml_name, "TextBox"))
    return "TextEdit";
  return xml_name;
}

static int runtime_count_children(xmlNodePtr node) {
  int n = 0;
  for (xmlNodePtr c = node ? node->children : NULL; c; c = c->next) {
    if (runtime_is_control_node(node, c))
      n++;
  }
  return n;
}

static form_ctrl_def_t *runtime_build_defs(runtime_build_ctx_t *ctx, xmlNodePtr parent,
                                           int *out_count);

static void runtime_fill_def(runtime_build_ctx_t *ctx, xmlNodePtr node,
                             form_ctrl_def_t *out) {
  memset(out, 0, sizeof(*out));

  char *flags_expr = runtime_xml_attr_dup(ctx, node, "flags");
  char *text = runtime_xml_attr_dup(ctx, node, "text");
  if (!text && runtime_is_report_column_node(node))
    text = runtime_xml_attr_dup(ctx, node, "title");
  char *name = runtime_xml_attr_dup(ctx, node, "name");
  char *h_align = runtime_xml_attr_dup(ctx, node, "h-align");
  if (!h_align) h_align = runtime_xml_attr_dup(ctx, node, "h_align");
  char *v_align = runtime_xml_attr_dup(ctx, node, "v-align");
  if (!v_align) v_align = runtime_xml_attr_dup(ctx, node, "v_align");
  char *font = runtime_xml_attr_dup(ctx, node, "font");
  char *color = runtime_xml_attr_dup(ctx, node, "color");
  char *padding = runtime_xml_attr_dup(ctx, node, "padding");
  char *margin = runtime_xml_attr_dup(ctx, node, "margin");
  char *orientation = runtime_xml_attr_dup(ctx, node, "orientation");
  if (!orientation) orientation = runtime_xml_attr_dup(ctx, node, "layout_orientation");

  out->class_name = runtime_strdup(ctx, (const char *)node->name);
  out->id = (uint32_t)runtime_xml_attr_int(node, "id", 0);
  out->size.w = (int16_t)runtime_xml_attr_int(node, "width", 0);
  out->size.h = (int16_t)runtime_xml_attr_int(node, "height", 0);
  out->flags = runtime_parse_flags(flags_expr);
  char *control_size = runtime_xml_attr_dup(ctx, node, "control-size");
  if (!control_size) control_size = runtime_xml_attr_dup(ctx, node, "control_size");
  if (control_size && str_ieq(control_size, "mini")) out->flags |= CONTROL_SIZE_MINI;
  else if (control_size && str_ieq(control_size, "small")) out->flags |= CONTROL_SIZE_SMALL;
  else if (control_size && str_ieq(control_size, "large")) out->flags |= CONTROL_SIZE_LARGE;
  else if (control_size && !str_ieq(control_size, "regular")) {
    fprintf(stderr, "[form] invalid control-size='%s' class=%s; using regular\n",
            control_size, (const char *)node->name);
    fflush(stderr);
  }
  out->text = text ? text : "";
  out->name = name ? name : "";
  out->layout_spacing = (uint8_t)runtime_xml_attr_int(node, "spacing", 4);
  out->h_align = runtime_parse_align_h(h_align);
  out->v_align = runtime_parse_align_v(v_align);
  out->font = runtime_parse_font(font, &out->font_set);
  out->color = runtime_parse_color(color, &out->color_set);

  if (orientation && str_ieq(orientation, "horizontal"))
    out->flags |= WINDOW_STACK_HORIZONTAL;

  if (str_ieq((const char *)node->name, "Space") ||
      str_ieq((const char *)node->name, "MultiEdit")) {
    out->flags |= WINDOW_FLEXSPACE;
  }

  parse_rect_attr(&out->padding, padding);
  parse_rect_attr(&out->margin, margin);

  if (xmlStrcasecmp(node->name, BAD_CAST "TableView") == 0) {
    runtime_fill_table_params(ctx, node, &out->lparam);
  } else if (xmlStrcasecmp(node->name, BAD_CAST "ComboBox") == 0) {
    runtime_fill_combo_params(ctx, node, &out->lparam);
  }

  out->children = runtime_build_defs(ctx, node, &out->child_count);
  const char *mapped = runtime_map_class_name(node);
  out->class_name = runtime_strdup(ctx, mapped);
  if (out->child_count > 0 &&
      xmlStrcasecmp(node->name, BAD_CAST "TableView") != 0 &&
      xmlStrcasecmp(node->name, BAD_CAST "ReportView") != 0)
    out->flags |= WINDOW_AUTO_LAYOUT;
}

static form_ctrl_def_t *runtime_build_defs(runtime_build_ctx_t *ctx, xmlNodePtr parent,
                                           int *out_count) {
  int count = runtime_count_children(parent);
  if (out_count)
    *out_count = count;
  if (count <= 0)
    return NULL;

  form_ctrl_def_t *defs = (form_ctrl_def_t *)runtime_alloc(ctx, (size_t)count * sizeof(form_ctrl_def_t));
  if (!defs)
    return NULL;

  int i = 0;
  for (xmlNodePtr c = parent ? parent->children : NULL; c; c = c->next) {
    if (!runtime_is_control_node(parent, c))
      continue;
    runtime_fill_def(ctx, c, &defs[i]);
    if (defs[i].id == 0)
      defs[i].id = (uint32_t)(ctx->next_generated_id++);
    i++;
  }

  return defs;
}

window_t *fe_create_runtime_form_window(window_t *doc,
                                        window_t *parent,
                                        winproc_t proc) {
  if (!doc || !parent || !proc)
    return NULL;

  runtime_build_ctx_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.next_generated_id = CTRL_ID_BASE;

  form_def_t def;
  memset(&def, 0, sizeof(def));
  irect16_t cr = get_client_rect(doc);
  def.name = doc->title;
  def.width = (cr.w > 0) ? cr.w : FORM_DEFAULT_W;
  def.height = (cr.h > 0) ? cr.h : FORM_DEFAULT_H;
  def.flags = WINDOW_NOTITLE | WINDOW_AUTO_LAYOUT;
  def.layout_spacing = 4;
  def.padding = (irect16_t){0, 0, 0, 0};
  def.margin = (irect16_t){0, 0, 0, 0};
  def.children = NULL;
  def.child_count = 0;

  xmlNodePtr form_node = (xmlNodePtr)doc->userdata2;
  if (form_node && form_node->type == XML_ELEMENT_NODE &&
      xmlStrcasecmp(form_node->name, BAD_CAST "form") == 0) {
    char flags_expr[128] = {0};
    runtime_xml_attr_copy(form_node, "flags", flags_expr, sizeof(flags_expr));
    def.flags = WINDOW_NOTITLE | WINDOW_AUTO_LAYOUT | runtime_parse_flags(flags_expr);
    def.layout_spacing = (uint8_t)runtime_xml_attr_int(form_node, "spacing", 4);

    char rect_expr[64] = {0};
    runtime_xml_attr_copy(form_node, "padding", rect_expr, sizeof(rect_expr));
    parse_rect_attr(&def.padding, rect_expr);
    rect_expr[0] = '\0';
    runtime_xml_attr_copy(form_node, "margin", rect_expr, sizeof(rect_expr));
    parse_rect_attr(&def.margin, rect_expr);

      // The document shell owns toolbar/statusbar chrome in FormEditor preview.
      // Keep the runtime root content-only so child layout matches the form client area.
      def.flags &= ~(WINDOW_TOOLBAR | WINDOW_STATUSBAR);
      def.toolbar_items = NULL;
      def.toolbar_count = 0;

    def.child_count = 0;
    def.children = runtime_build_defs(&ctx, form_node, &def.child_count);
  }

  window_t *root = create_window_from_form(&def, 0, 0, parent, proc, 0, NULL);

  runtime_release(&ctx);
  return root;
}
