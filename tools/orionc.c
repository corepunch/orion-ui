#include <libxml/parser.h>
#include <libxml/tree.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  int x, y, w, h;
} frame_t;

typedef struct {
  char name[128];
  char value[64];
} define_t;

typedef struct {
  define_t items[512];
  int count;
} define_list_t;

static const char *base_name(const char *path) {
  const char *s = strrchr(path, '/');
  return s ? s + 1 : path;
}

static bool streq(const char *a, const char *b) {
  return a && b && strcmp(a, b) == 0;
}

static bool is_ident_expr(const char *s) {
  if (!s || !*s) return false;
  unsigned char c = (unsigned char)*s++;
  if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'))
    return false;
  while (*s) {
    c = (unsigned char)*s++;
    if (!((c >= 'A' && c <= 'Z') ||
          (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') ||
          c == '_'))
      return false;
  }
  return true;
}

static char *attr_dup(xmlNodePtr node, const char *name) {
  xmlChar *raw = xmlGetProp(node, BAD_CAST name);
  if (!raw) return NULL;
  char *s = strdup((const char *)raw);
  xmlFree(raw);
  return s;
}

static const char *nonempty(const char *s, const char *fallback) {
  return (s && *s) ? s : fallback;
}

static bool parse_frame(xmlNodePtr node, frame_t *out) {
  if (!node || !out) return false;
  char *frame = attr_dup(node, "frame");
  if (frame) {
    bool ok = sscanf(frame, "%d %d %d %d", &out->x, &out->y, &out->w, &out->h) == 4;
    free(frame);
    if (ok) return true;
  }

  char *x = attr_dup(node, "x");
  char *y = attr_dup(node, "y");
  char *w = attr_dup(node, "w");
  char *h = attr_dup(node, "h");
  if (!w) w = attr_dup(node, "width");
  if (!h) h = attr_dup(node, "height");
  if (x && y && w && h) {
    out->x = atoi(x);
    out->y = atoi(y);
    out->w = atoi(w);
    out->h = atoi(h);
    free(x); free(y); free(w); free(h);
    return true;
  }
  free(x); free(y); free(w); free(h);
  return false;
}

static void fprint_c_string(FILE *f, const char *s) {
  fputc('"', f);
  if (s) {
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
      switch (*p) {
        case '\\': fputs("\\\\", f); break;
        case '"':  fputs("\\\"", f); break;
        case '\n': fputs("\\n", f); break;
        case '\r': fputs("\\r", f); break;
        case '\t': fputs("\\t", f); break;
        default:
          if (*p < 0x20)
            fprintf(f, "\\x%02x", *p);
          else
            fputc(*p, f);
          break;
      }
    }
  }
  fputc('"', f);
}

static void fprint_c_string_with_shortcut(FILE *f, const char *label,
                                          const char *shortcut) {
  fputc('"', f);
  const char *parts[3] = { label, (shortcut && *shortcut) ? "\t" : NULL, shortcut };
  for (int i = 0; i < 3; i++) {
    const char *s = parts[i];
    if (!s) continue;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
      switch (*p) {
        case '\\': fputs("\\\\", f); break;
        case '"':  fputs("\\\"", f); break;
        case '\n': fputs("\\n", f); break;
        case '\r': fputs("\\r", f); break;
        case '\t': fputs("\\t", f); break;
        default:
          if (*p < 0x20)
            fprintf(f, "\\x%02x", *p);
          else
            fputc(*p, f);
          break;
      }
    }
  }
  fputc('"', f);
}

static void make_ident(char *out, size_t out_sz, const char *s) {
  size_t n = 0;
  if (!out || out_sz == 0) return;
  for (const unsigned char *p = (const unsigned char *)nonempty(s, "form");
       *p && n + 1 < out_sz; p++) {
    bool ok = (*p >= 'a' && *p <= 'z') ||
              (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9');
    out[n++] = ok ? (char)*p : '_';
  }
  if (n == 0) out[n++] = '_';
  out[n] = '\0';
}

static bool is_element(xmlNodePtr node, const char *name) {
  return node && node->type == XML_ELEMENT_NODE &&
         xmlStrcmp(node->name, BAD_CAST name) == 0;
}

static xmlNodePtr first_child_element(xmlNodePtr node, const char *name) {
  for (xmlNodePtr c = node ? node->children : NULL; c; c = c->next)
    if (is_element(c, name)) return c;
  return NULL;
}

static bool attr_is_true(xmlNodePtr node, const char *name) {
  char *v = attr_dup(node, name);
  bool yes = v && (!strcmp(v, "true") || !strcmp(v, "1") || !strcmp(v, "yes"));
  free(v);
  return yes;
}

static int count_controls(xmlNodePtr form) {
  int n = 0;
  xmlNodePtr controls = first_child_element(form, "controls");
  for (xmlNodePtr c = controls ? controls->children : NULL; c; c = c->next)
    if (is_element(c, "control")) n++;
  return n;
}

static bool collect_define(define_list_t *defs, const char *name,
                           const char *value) {
  if (!defs || !is_ident_expr(name) || !value || !*value) return true;
  for (int i = 0; i < defs->count; i++) {
    if (!strcmp(defs->items[i].name, name)) {
      if (!strcmp(defs->items[i].value, value)) return true;
      fprintf(stderr, "orionc: conflicting values for id '%s' (%s vs %s)\n",
              name, defs->items[i].value, value);
      return false;
    }
  }
  if (defs->count >= (int)(sizeof(defs->items) / sizeof(defs->items[0]))) {
    fprintf(stderr, "orionc: too many generated id defines\n");
    return false;
  }
  snprintf(defs->items[defs->count].name, sizeof(defs->items[defs->count].name),
           "%s", name);
  snprintf(defs->items[defs->count].value, sizeof(defs->items[defs->count].value),
           "%s", value);
  defs->count++;
  return true;
}

static bool collect_form_defines(define_list_t *defs, xmlNodePtr form) {
  xmlNodePtr controls = first_child_element(form, "controls");
  for (xmlNodePtr c = controls ? controls->children : NULL; c; c = c->next) {
    if (!is_element(c, "control")) continue;
    char *cid = attr_dup(c, "id");
    char *value = attr_dup(c, "value");
    bool ok = collect_define(defs, cid, value);
    free(cid);
    free(value);
    if (!ok) return false;
  }
  return true;
}

static bool collect_menu_node_defines(define_list_t *defs, xmlNodePtr menu) {
  for (xmlNodePtr it = menu ? menu->children : NULL; it; it = it->next) {
    if (is_element(it, "submenu")) {
      if (!collect_menu_node_defines(defs, it)) return false;
      continue;
    }
    if (!is_element(it, "item")) continue;
    char *id = attr_dup(it, "id");
    char *value = attr_dup(it, "value");
    bool ok = collect_define(defs, id, value);
    free(id);
    free(value);
    if (!ok) return false;
  }
  return true;
}

static bool collect_menu_defines(define_list_t *defs, xmlNodePtr menus) {
  for (xmlNodePtr m = menus ? menus->children : NULL; m; m = m->next) {
    if (!is_element(m, "menu")) continue;
    if (!collect_menu_node_defines(defs, m)) return false;
  }
  return true;
}

static const char *toolbar_item_type_for_node(xmlNodePtr node);

static bool collect_toolbar_defines(define_list_t *defs, xmlNodePtr toolbars) {
  for (xmlNodePtr tb = toolbars ? toolbars->children : NULL; tb; tb = tb->next) {
    if (!is_element(tb, "toolbar")) continue;
    for (xmlNodePtr it = tb->children; it; it = it->next) {
      if (!toolbar_item_type_for_node(it)) continue;
      char *id = attr_dup(it, "id");
      char *value = attr_dup(it, "value");
      bool ok = collect_define(defs, id, value);
      free(id);
      free(value);
      if (!ok) return false;
    }
  }
  return true;
}

static void emit_defines(FILE *f, const define_list_t *defs) {
  if (!defs || defs->count <= 0) return;
  fputs("/* IDs generated from symbolic id/value pairs. */\n", f);
  for (int i = 0; i < defs->count; i++)
    fprintf(f, "#define %-20s %s\n", defs->items[i].name, defs->items[i].value);
  fputc('\n', f);
}

static int count_menu_items(xmlNodePtr menu) {
  int n = 0;
  for (xmlNodePtr it = menu ? menu->children : NULL; it; it = it->next)
    if (is_element(it, "item") || is_element(it, "separator") ||
        is_element(it, "submenu")) n++;
  return n;
}

static void emit_optional_if(FILE *f, xmlNodePtr node) {
  char *expr = attr_dup(node, "if");
  if (expr && *expr)
    fprintf(f, "#if %s\n", expr);
  free(expr);
}

static void emit_optional_endif(FILE *f, xmlNodePtr node) {
  char *expr = attr_dup(node, "if");
  if (expr && *expr)
    fputs("#endif\n", f);
  free(expr);
}

static void emit_menu_indices(FILE *f, xmlNodePtr menus) {
  if (!menus) return;
  int idx = 0;
  bool emitted = false;
  for (xmlNodePtr m = menus->children; m; m = m->next) {
    if (!is_element(m, "menu")) continue;
    char *id = attr_dup(m, "id");
    if (is_ident_expr(id)) {
      if (!emitted) {
        fputs("/* Top-level menu indices generated from <menu> order. */\n", f);
        emitted = true;
      }
      char ident[128];
      make_ident(ident, sizeof(ident), id);
      for (char *p = ident; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');
      fprintf(f, "#define MENU_%s_INDEX %d\n", ident, idx);
    }
    free(id);
    idx++;
  }
  if (emitted)
    fputc('\n', f);
}

static void menu_child_var(char *out, size_t out_sz,
                           const char *parent_var, xmlNodePtr node) {
  char *var = attr_dup(node, "var");
  if (var && *var) {
    snprintf(out, out_sz, "%s", var);
    free(var);
    return;
  }
  free(var);

  char *id = attr_dup(node, "id");
  char *label = attr_dup(node, "label");
  char ident[128];
  make_ident(ident, sizeof(ident), nonempty(id, nonempty(label, "submenu")));
  snprintf(out, out_sz, "%s_%s", nonempty(parent_var, "kMenu"), ident);
  free(id);
  free(label);
}

static bool emit_menu_item_array(FILE *f, xmlNodePtr menu,
                                 const char *var, bool is_mutable);

static bool emit_submenu_arrays(FILE *f, xmlNodePtr menu,
                                const char *parent_var, bool is_mutable) {
  for (xmlNodePtr it = menu ? menu->children : NULL; it; it = it->next) {
    if (!is_element(it, "submenu")) continue;
    char child_var[256];
    menu_child_var(child_var, sizeof(child_var), parent_var, it);
    if (!emit_submenu_arrays(f, it, child_var, is_mutable))
      return false;
    if (!emit_menu_item_array(f, it, child_var, is_mutable))
      return false;
  }
  return true;
}

static bool emit_menu_item_array(FILE *f, xmlNodePtr menu,
                                 const char *var, bool is_mutable) {
  int item_count = count_menu_items(menu);
  if (item_count <= 0) return true;
  fprintf(f, "static %smenu_item_t %s[] = {\n",
          is_mutable ? "" : "const ", var);
  for (xmlNodePtr it = menu->children; it; it = it->next) {
    emit_optional_if(f, it);
    if (is_element(it, "separator")) {
      fputs("  { NULL, 0, NULL, 0 },\n", f);
      emit_optional_endif(f, it);
      continue;
    }
    if (is_element(it, "submenu")) {
      char child_var[256];
      char *label = attr_dup(it, "label");
      char *count = attr_dup(it, "count");
      bool dynamic = attr_is_true(it, "dynamic");
      int child_count = count_menu_items(it);
      menu_child_var(child_var, sizeof(child_var), var, it);
      fputs("  { ", f);
      fprint_c_string(f, nonempty(label, ""));
      if (dynamic && child_count <= 0) {
        fprintf(f, ", 0, NULL, %s },\n", nonempty(count, "0"));
      } else if (child_count <= 0) {
        fputs(", 0, NULL, 0 },\n", f);
      } else if (count && *count) {
        fprintf(f, ", 0, %s, %s },\n", child_var, count);
      } else {
        fprintf(f, ", 0, %s, (int)(sizeof(%s) / sizeof(%s[0])) },\n",
                child_var, child_var, child_var);
      }
      free(label);
      free(count);
      emit_optional_endif(f, it);
      continue;
    }
    if (!is_element(it, "item")) {
      emit_optional_endif(f, it);
      continue;
    }
    char *id = attr_dup(it, "id");
    char *label = attr_dup(it, "label");
    char *shortcut = attr_dup(it, "shortcut");
    fputs("  { ", f);
    fprint_c_string_with_shortcut(f, nonempty(label, ""), shortcut);
    fprintf(f, ", %s, NULL, 0 },\n", nonempty(id, "0"));
    free(id);
    free(label);
    free(shortcut);
    emit_optional_endif(f, it);
  }
  fputs("};\n\n", f);
  return true;
}

static bool emit_menu_resources(FILE *f, xmlNodePtr menus) {
  if (!menus) return true;
  char *menus_var = attr_dup(menus, "var");
  char *count_var = attr_dup(menus, "count");
  const char *menu_array = nonempty(menus_var, "kMenus");
  const char *menu_count = nonempty(count_var, "kNumMenus");

  for (xmlNodePtr m = menus->children; m; m = m->next) {
    if (!is_element(m, "menu")) continue;
    char *var = attr_dup(m, "var");
    int item_count = count_menu_items(m);
    if (item_count <= 0) {
      free(var);
      continue;
    }
    if (!var || !*var) {
      fprintf(stderr, "orionc: menu with items has no var\n");
      free(var); free(menus_var); free(count_var);
      return false;
    }
    bool is_mutable = attr_is_true(m, "mutable");
    if (!emit_submenu_arrays(f, m, var, is_mutable) ||
        !emit_menu_item_array(f, m, var, is_mutable)) {
      free(var); free(menus_var); free(count_var);
      return false;
    }
    free(var);
  }

  fprintf(f, "static menu_def_t %s[] = {\n", menu_array);
  for (xmlNodePtr m = menus->children; m; m = m->next) {
    if (!is_element(m, "menu")) continue;
    char *label = attr_dup(m, "label");
    char *var = attr_dup(m, "var");
    char *count = attr_dup(m, "count");
    bool dynamic = attr_is_true(m, "dynamic");
    int item_count = count_menu_items(m);

    fputs("  { ", f);
    fprint_c_string(f, nonempty(label, ""));
    if (dynamic && item_count <= 0) {
      fprintf(f, ", NULL, %s },\n", nonempty(count, "0"));
    } else if (count && *count) {
      fprintf(f, ", %s, %s },\n", nonempty(var, "NULL"), count);
    } else {
      fprintf(f, ", %s, (int)(sizeof(%s) / sizeof(%s[0])) },\n",
              nonempty(var, "NULL"), nonempty(var, "NULL"), nonempty(var, "NULL"));
    }
    free(label);
    free(var);
    free(count);
  }
  fputs("};\n", f);
  fprintf(f, "static const int %s = (int)(sizeof(%s) / sizeof(%s[0]));\n\n",
          menu_count, menu_array, menu_array);

  free(menus_var);
  free(count_var);
  return true;
}

static const char *toolbar_item_type_for_node(xmlNodePtr node) {
  if (is_element(node, "button")) return "TOOLBAR_ITEM_BUTTON";
  if (is_element(node, "label")) return "TOOLBAR_ITEM_LABEL";
  if (is_element(node, "combobox")) return "TOOLBAR_ITEM_COMBOBOX";
  if (is_element(node, "textedit")) return "TOOLBAR_ITEM_TEXTEDIT";
  if (is_element(node, "separator")) return "TOOLBAR_ITEM_SEPARATOR";
  if (is_element(node, "spacer")) return "TOOLBAR_ITEM_SPACER";
  return NULL;
}

static int count_toolbar_items(xmlNodePtr toolbar) {
  int n = 0;
  for (xmlNodePtr it = toolbar ? toolbar->children : NULL; it; it = it->next)
    if (toolbar_item_type_for_node(it)) n++;
  return n;
}

static bool emit_toolbar_resources(FILE *f, xmlNodePtr toolbars) {
  if (!toolbars) return true;

  for (xmlNodePtr tb = toolbars->children; tb; tb = tb->next) {
    if (!is_element(tb, "toolbar")) continue;
    char *var = attr_dup(tb, "var");
    char *count = attr_dup(tb, "count");
    int item_count = count_toolbar_items(tb);
    if (item_count <= 0) {
      free(var);
      free(count);
      continue;
    }
    if (!var || !*var) {
      fprintf(stderr, "orionc: toolbar with items has no var\n");
      free(var);
      free(count);
      return false;
    }

    fprintf(f, "static const toolbar_item_t %s[] = {\n", var);
    for (xmlNodePtr it = tb->children; it; it = it->next) {
      const char *type = toolbar_item_type_for_node(it);
      if (!type) continue;

      char *id = attr_dup(it, "id");
      char *icon = attr_dup(it, "icon");
      char *w = attr_dup(it, "w");
      char *flags = attr_dup(it, "flags");
      char *text = attr_dup(it, "text");
      fprintf(f, "  { %s, %s, %s, %s, %s, ",
              type,
              nonempty(id, "0"),
              nonempty(icon, "-1"),
              nonempty(w, "0"),
              nonempty(flags, "0"));
      if (text && *text)
        fprint_c_string(f, text);
      else
        fputs("NULL", f);
      fputs(" },\n", f);
      free(id);
      free(icon);
      free(w);
      free(flags);
      free(text);
    }
    fputs("};\n", f);
    if (count && *count)
      fprintf(f, "static const int %s = (int)(sizeof(%s) / sizeof(%s[0]));\n",
              count, var, var);
    fputc('\n', f);
    free(var);
    free(count);
  }

  return true;
}

static bool emit_form(FILE *f, xmlNodePtr form, const char *prefix) {
  char *id = attr_dup(form, "id");
  char *title = attr_dup(form, "title");
  char *flags = attr_dup(form, "flags");
  frame_t fr = {0, 0, 0, 0};
  if (!parse_frame(form, &fr)) {
    fprintf(stderr, "orionc: form '%s' has no valid frame\n", nonempty(id, ""));
    free(id); free(title); free(flags);
    return false;
  }

  char id_ident[128];
  make_ident(id_ident, sizeof(id_ident), id);
  fprintf(f, "static const form_ctrl_def_t %s_%s_children[] = {\n",
          prefix, id_ident);

  xmlNodePtr controls = first_child_element(form, "controls");
  for (xmlNodePtr c = controls ? controls->children : NULL; c; c = c->next) {
    if (!is_element(c, "control")) continue;
    char *klass = attr_dup(c, "class");
    char *cid = attr_dup(c, "id");
    char *name = attr_dup(c, "name");
    char *text = attr_dup(c, "text");
    char *cflags = attr_dup(c, "flags");
    frame_t cr = {0, 0, 0, 0};
    if (!parse_frame(c, &cr)) {
      fprintf(stderr, "orionc: control '%s' in form '%s' has no valid frame\n",
              nonempty(name, ""), nonempty(id, ""));
      free(klass); free(cid); free(name); free(text); free(cflags);
      free(id); free(title); free(flags);
      return false;
    }

    fputs("  { ", f);
    fprint_c_string(f, nonempty(klass, ""));
    fprintf(f, ", %s, { %d, %d, %d, %d }, %s, ",
            nonempty(cid, "0"), cr.x, cr.y, cr.w, cr.h, nonempty(cflags, "0"));
    fprint_c_string(f, nonempty(text, ""));
    fputs(", ", f);
    fprint_c_string(f, nonempty(name, ""));
    fputs(" },\n", f);

    free(klass); free(cid); free(name); free(text); free(cflags);
  }

  int child_count = count_controls(form);
  fprintf(f, "};\n\n");
  fprintf(f, "static const form_def_t %s_%s_form = {\n", prefix, id_ident);
  fputs("  .name = ", f);
  fprint_c_string(f, nonempty(title, nonempty(id, "")));
  fprintf(f, ",\n  .width = %d,\n  .height = %d,\n", fr.w, fr.h);
  fprintf(f, "  .flags = %s,\n", nonempty(flags, "0"));
  fprintf(f, "  .children = %s_%s_children,\n", prefix, id_ident);
  fprintf(f, "  .child_count = %d,\n", child_count);
  fputs("};\n\n", f);

  free(id); free(title); free(flags);
  return true;
}

static void usage(const char *argv0) {
  fprintf(stderr,
          "usage: %s --input file.orion --output forms.h --prefix name [--form id]\n",
          base_name(argv0));
}

int main(int argc, char **argv) {
  const char *input = NULL;
  const char *output = NULL;
  const char *prefix = "orion";
  const char *only_form = NULL;

  for (int i = 1; i < argc; i++) {
    if (streq(argv[i], "--input") && i + 1 < argc) input = argv[++i];
    else if (streq(argv[i], "--output") && i + 1 < argc) output = argv[++i];
    else if (streq(argv[i], "--prefix") && i + 1 < argc) prefix = argv[++i];
    else if (streq(argv[i], "--form") && i + 1 < argc) only_form = argv[++i];
    else {
      usage(argv[0]);
      return 2;
    }
  }
  if (!input || !output) {
    usage(argv[0]);
    return 2;
  }

  char prefix_ident[128];
  make_ident(prefix_ident, sizeof(prefix_ident), prefix);

  xmlDocPtr doc = xmlReadFile(input, NULL, XML_PARSE_NONET);
  if (!doc) {
    fprintf(stderr, "orionc: failed to read %s\n", input);
    return 1;
  }
  xmlNodePtr root = xmlDocGetRootElement(doc);
  if (!is_element(root, "orion")) {
    fprintf(stderr, "orionc: %s is not an <orion> document\n", input);
    xmlFreeDoc(doc);
    return 1;
  }

  FILE *f = fopen(output, "wb");
  if (!f) {
    perror(output);
    xmlFreeDoc(doc);
    return 1;
  }

  char guard[256];
  make_ident(guard, sizeof(guard), output);
  for (char *p = guard; *p; p++)
    if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');

  fprintf(f, "/* Generated by orionc from %s. */\n", input);
  fprintf(f, "#ifndef %s\n#define %s\n\n", guard, guard);
  fputs("#include \"ui.h\"\n\n", f);

  int emitted = 0;
  define_list_t defines = {0};
  xmlNodePtr menus = first_child_element(root, "menus");
  xmlNodePtr toolbars = first_child_element(root, "toolbars");
  xmlNodePtr forms = first_child_element(root, "forms");

  if (!collect_menu_defines(&defines, menus)) {
    fclose(f);
    xmlFreeDoc(doc);
    return 1;
  }
  if (!collect_toolbar_defines(&defines, toolbars)) {
    fclose(f);
    xmlFreeDoc(doc);
    return 1;
  }

  for (xmlNodePtr n = forms ? forms->children : NULL; n; n = n->next) {
    if (!is_element(n, "form")) continue;
    char *id = attr_dup(n, "id");
    bool want = !only_form || streq(id, only_form);
    free(id);
    if (!want) continue;
    if (!collect_form_defines(&defines, n)) {
      fclose(f);
      xmlFreeDoc(doc);
      return 1;
    }
  }
  emit_defines(f, &defines);
  emit_menu_indices(f, menus);
  if (!emit_menu_resources(f, menus)) {
    fclose(f);
    xmlFreeDoc(doc);
    return 1;
  }
  if (!emit_toolbar_resources(f, toolbars)) {
    fclose(f);
    xmlFreeDoc(doc);
    return 1;
  }

  for (xmlNodePtr n = forms ? forms->children : NULL; n; n = n->next) {
    if (!is_element(n, "form")) continue;
    char *id = attr_dup(n, "id");
    bool want = !only_form || streq(id, only_form);
    free(id);
    if (!want) continue;
    if (!emit_form(f, n, prefix_ident)) {
      fclose(f);
      xmlFreeDoc(doc);
      return 1;
    }
    emitted++;
  }

  fprintf(f, "#endif /* %s */\n", guard);
  fclose(f);
  xmlFreeDoc(doc);

  if (emitted == 0) {
    fprintf(stderr, "orionc: no forms emitted from %s\n", input);
    return 1;
  }
  return 0;
}
