// Menu bar, document management, file I/O, and dialog entry points
// for the Orion Form Editor.

#include "formeditor.h"
#include "../../commctl/commctl.h"
#include <inttypes.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

// ============================================================
// Menu definitions
// ============================================================

static const menu_item_t kFileItems[] = {
  {"New",        ID_FILE_NEW},
  {"Open...",    ID_FILE_OPEN},
  {NULL,         0},
  {"Save",       ID_FILE_SAVE},
  {"Save As...", ID_FILE_SAVEAS},
  {NULL,         0},
  {"Quit",       ID_FILE_QUIT},
};

static const menu_item_t kEditItems[] = {
  {"Delete",            ID_EDIT_DELETE},
  {NULL,                0},
  {"Properties...",     ID_EDIT_PROPS},
};

static const menu_item_t kViewItems[] = {
  {"Grid Settings...", ID_VIEW_GRID},
};

static const menu_item_t kHelpItems[] = {
  {"About...", ID_HELP_ABOUT},
};

menu_def_t kMenus[] = {
  {"File", kFileItems, (int)(sizeof(kFileItems)/sizeof(kFileItems[0]))},
  {"Edit", kEditItems, (int)(sizeof(kEditItems)/sizeof(kEditItems[0]))},
  {"View", kViewItems, (int)(sizeof(kViewItems)/sizeof(kViewItems[0]))},
  {"Help", kHelpItems, (int)(sizeof(kHelpItems)/sizeof(kHelpItems[0]))},
};
const int kNumMenus = (int)(sizeof(kMenus)/sizeof(kMenus[0]));

// ============================================================
// Document title
// ============================================================

void form_doc_update_title(form_doc_t *doc) {
  if (!doc || !doc->doc_win) return;
  const char *name = doc->form_title[0] ? doc->form_title :
                     (doc->form_id[0] ? doc->form_id : "Untitled");
  const char *slash = strrchr(name, '/');
  if (slash) name = slash + 1;
  snprintf(doc->doc_win->title, sizeof(doc->doc_win->title), "%s%s",
           name, doc->modified ? " *" : "");
  invalidate_window(doc->doc_win);
}

void form_doc_activate(form_doc_t *doc) {
  if (!g_app || !doc) return;
  if (g_app->doc == doc) return;
  form_doc_t *prev = g_app->doc;
  g_app->doc = doc;
  if (prev && prev->doc_win)
    invalidate_window(prev->doc_win);
  if (doc->doc_win)
    invalidate_window(doc->doc_win);
  property_browser_refresh(doc);
  forms_browser_refresh();
}

void form_doc_show_only(form_doc_t *doc) {
  if (!g_app || !doc) return;
  for (form_doc_t *it = g_app->docs; it; it = it->next) {
    if (it != doc && it->doc_win && is_window(it->doc_win))
      show_window(it->doc_win, false);
  }
  form_doc_activate(doc);
  if (doc->doc_win && is_window(doc->doc_win))
    show_window(doc->doc_win, true);
  forms_browser_refresh();
}

// ============================================================
// Document window procedure
// ============================================================

static result_t doc_win_proc(window_t *win, uint32_t msg,
                              uint32_t wparam, void *lparam) {
  form_doc_t *doc = (form_doc_t *)win->userdata;
  switch (msg) {
    case evCreate:
      return true;
    case evSetFocus:
      if (doc && win->visible) form_doc_activate(doc);
      return false;
    case evPaint:
      fill_rect(get_sys_color(brWorkspaceBg), R(0, 0, win->frame.w, win->frame.h));
      return false;
    case evHScroll:
      // Forward the built-in hscroll notification to the canvas child.
      if (doc && doc->canvas_win)
        send_message(doc->canvas_win, evHScroll, wparam, lparam);
      return true;
    case evResize: {
      if (doc && doc->canvas_win) {
        irect16_t cr = get_client_rect(win);
        int new_w = MAX(1, cr.w);
        int new_h = MAX(1, cr.h);
        bool changed = (doc->form_size.w != new_w || doc->form_size.h != new_h);
        doc->form_size.w = new_w;
        doc->form_size.h = new_h;
        resize_window(doc->canvas_win, cr.w, cr.h);
        if (changed) {
          doc->modified = true;
          if (g_app)
            g_app->project.modified = true;
          form_doc_update_title(doc);
        }
      }
      return false;
    }
    case evClose: {
      if (!doc) return false;
      show_window(win, false);
      forms_browser_refresh();
      return true;
    }
    default:
      return false;
  }
}

// ============================================================
// create_form_doc / close_form_doc
// ============================================================

static irect16_t form_doc_frame_for_size(int form_w, int form_h, uint32_t form_flags) {
  int max_w = SCREEN_W - 4;
  int max_h = SCREEN_H - MENUBAR_HEIGHT - 4;
  bool has_status = (form_flags & WINDOW_STATUSBAR) != 0;
  int status_h = has_status ? STATUSBAR_HEIGHT : 0;
  bool needs_hscroll = form_w > max_w;
  int hstrip = (needs_hscroll && !has_status) ? SCROLLBAR_WIDTH : 0;
  int max_canvas_h = max_h - TITLEBAR_HEIGHT - status_h - hstrip;
  bool needs_vscroll;
  int frame_w;
  int frame_h;

  if (max_w < 1) max_w = 1;
  if (max_canvas_h < 1) max_canvas_h = 1;

  needs_vscroll = form_h > max_canvas_h;
  frame_w = form_w + (needs_vscroll ? SCROLLBAR_WIDTH : 0);
  if (frame_w > max_w) frame_w = max_w;

  frame_h = TITLEBAR_HEIGHT + status_h + hstrip + form_h;
  if (frame_h > max_h) frame_h = max_h;

  return (irect16_t){CW_USEDEFAULT, CW_USEDEFAULT, frame_w, frame_h};
}

static void form_doc_apply_window_flags_and_size(form_doc_t *doc) {
  if (!doc || !doc->doc_win) return;
  doc->doc_win->flags &= ~WINDOW_STATUSBAR;
  doc->doc_win->flags |= (doc->flags & WINDOW_STATUSBAR);
  irect16_t frame = form_doc_frame_for_size(doc->form_size.w, doc->form_size.h, doc->flags);
  resize_window(doc->doc_win, frame.w, frame.h);
  if (doc->canvas_win) {
    irect16_t cr = get_client_rect(doc->doc_win);
    resize_window(doc->canvas_win, cr.w, cr.h);
  }
}

form_doc_t *create_form_doc(int w, int h) {
  if (!g_app) return NULL;
  if (w <= 0 || h <= 0 || w > INT16_MAX || h > INT16_MAX) return NULL;
  form_doc_t *prev_doc = g_app->doc;

  form_doc_t *doc = (form_doc_t *)calloc(1, sizeof(form_doc_t));
  if (!doc) return NULL;

  doc->form_size.w    = w;
  doc->form_size.h    = h;
  doc->flags     = 0;
  doc->modified  = false;
  doc->next_id   = CTRL_ID_BASE;
  doc->grid_size    = 8;
  doc->show_grid    = true;
  doc->snap_to_grid = true;

  // Document window
  irect16_t doc_frame = form_doc_frame_for_size(w, h, doc->flags);
  set_default_window_position(DOC_START_X, DOC_START_Y);
  window_t *dwin = create_window(
      "Untitled",
      WINDOW_HSCROLL | (doc->flags & WINDOW_STATUSBAR),
      &doc_frame,
      NULL, doc_win_proc, g_app->hinstance, NULL);
  dwin->userdata = doc;
  doc->doc_win   = dwin;

  // Canvas child window (owns the VSCROLL) — sized to the document window's client area
  irect16_t cr = get_client_rect(dwin);
  window_t *cwin = create_window(
      "", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL,
      MAKERECT(0, 0, cr.w, cr.h),
      dwin, win_canvas_proc, 0, doc);
  cwin->flags &= ~WINDOW_NOTABSTOP;
  doc->canvas_win = cwin;
  cr = get_client_rect(dwin);
  resize_window(cwin, cr.w, cr.h);

  doc->next = NULL;
  if (!g_app->docs) {
    g_app->docs = doc;
  } else {
    form_doc_t *tail = g_app->docs;
    while (tail->next)
      tail = tail->next;
    tail->next = doc;
  }
  g_app->doc = doc;

  show_window(dwin, true);
  if (prev_doc && prev_doc->doc_win)
    invalidate_window(prev_doc->doc_win);
  form_doc_update_title(doc);
  send_message(dwin, evStatusBar, 0, (void *)"New form");
  property_browser_refresh(doc);
  forms_browser_refresh();
  return doc;
}

void close_form_doc(form_doc_t *doc) {
  if (!doc) return;
  if (g_app) {
    form_doc_t **link = &g_app->docs;
    while (*link && *link != doc)
      link = &(*link)->next;
    if (*link == doc)
      *link = doc->next;
    if (g_app->doc == doc)
      g_app->doc = g_app->docs;
  }
  if (doc->doc_win && is_window(doc->doc_win))
    destroy_window(doc->doc_win);
  property_browser_refresh(g_app ? g_app->doc : NULL);
  forms_browser_refresh();
  free(doc);
}

// ============================================================
// Project I/O — XML .orion files
// ============================================================

// Map control type to a short keyword used in the file.
static const char *ctrl_type_token(int type) {
  const fe_component_desc_t *c = fe_component_by_id(type);
  return c ? c->token : "control";
}

static int ctrl_type_from_token(const char *tok) {
  const fe_component_desc_t *c = fe_component_by_token(tok);
  if (!c) return -1;
  for (int i = 0; i < fe_component_count(); i++) {
    const fe_component_desc_t *it = fe_component_at(i);
    if (it == c)
      return i;
  }
  return -1;
}

// ============================================================
// XML project I/O (.orion)
// ============================================================

static bool has_ext(const char *path, const char *ext) {
  if (!path || !ext) return false;
  size_t n = strlen(path);
  size_t e = strlen(ext);
  if (n < e) return false;
  return strcmp(path + n - e, ext) == 0;
}

static char *xml_attr_dup(xmlNodePtr node, const char *name) {
  xmlChar *v = xmlGetProp(node, BAD_CAST name);
  if (!v) return NULL;
  char *s = strdup((const char *)v);
  xmlFree(v);
  return s;
}

static void copy_attr(xmlNodePtr node, const char *name, char *dst, size_t dst_sz) {
  char *v;
  if (!dst || dst_sz == 0) return;
  v = xml_attr_dup(node, name);
  if (!v) return;
  snprintf(dst, dst_sz, "%s", v);
  free(v);
}

static int int_attr(xmlNodePtr node, const char *name, int fallback) {
  char *v = xml_attr_dup(node, name);
  if (!v) return fallback;
  char *end = NULL;
  long n = strtol(v, &end, 0);
  int out = (end && *end == '\0') ? (int)n : fallback;
  free(v);
  return out;
}

static bool parse_numeric_expr(const char *s, int *out) {
  if (!s || !*s || !out) return false;
  char *end = NULL;
  long n = strtol(s, &end, 0);
  if (!end || *end != '\0') return false;
  *out = (int)n;
  return true;
}

static bool is_numeric_expr(const char *s) {
  int ignored = 0;
  return parse_numeric_expr(s, &ignored);
}

static uint32_t flag_value(const char *tok) {
  if (!tok || !*tok || strcmp(tok, "0") == 0) return 0;
  if (strcmp(tok, "BUTTON_DEFAULT") == 0) return BUTTON_DEFAULT;
  if (strcmp(tok, "WINDOW_NOTITLE") == 0) return WINDOW_NOTITLE;
  if (strcmp(tok, "WINDOW_NOFILL") == 0) return WINDOW_NOFILL;
  if (strcmp(tok, "WINDOW_NOTABSTOP") == 0) return WINDOW_NOTABSTOP;
  if (strcmp(tok, "WINDOW_STATUSBAR") == 0) return WINDOW_STATUSBAR;
  if (strcmp(tok, "WINDOW_DIALOG") == 0) return WINDOW_DIALOG;
  if (strcmp(tok, "WINDOW_NOTRAYBUTTON") == 0) return WINDOW_NOTRAYBUTTON;
  if (strcmp(tok, "WINDOW_NORESIZE") == 0) return WINDOW_NORESIZE;
  if (strcmp(tok, "WINDOW_HSCROLL") == 0) return WINDOW_HSCROLL;
  if (strcmp(tok, "WINDOW_VSCROLL") == 0) return WINDOW_VSCROLL;
  char *end = NULL;
  unsigned long n = strtoul(tok, &end, 0);
  return (end && *end == '\0') ? (uint32_t)n : 0;
}

static uint32_t parse_flags_expr(const char *expr) {
  if (!expr || !*expr) return 0;
  char buf[256];
  snprintf(buf, sizeof(buf), "%s", expr);
  uint32_t flags = 0;
  char *p = buf;
  while (*p) {
    while (*p == ' ' || *p == '\t' || *p == '|') p++;
    char *start = p;
    while (*p && *p != '|') p++;
    char save = *p;
    *p = '\0';
    char *end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
    flags |= flag_value(start);
    if (!save) break;
    p++;
  }
  return flags;
}

static bool load_component_plugin_named(const char *name) {
  if (!name || !*name) return false;
  if (strchr(name, '/') || has_ext(name, AX_DYNLIB_EXT))
    return fe_load_component_plugin(name);
  char path[4096];
  int n = snprintf(path, sizeof(path), "%s/../lib/%s%s",
                   ui_get_exe_dir(), name, AX_DYNLIB_EXT);
  if (n <= 0 || (size_t)n >= sizeof(path)) return false;
  return fe_load_component_plugin(path);
}

static int project_resolve_control_id(form_doc_t *doc, const char *expr,
                                      const char *value_expr) {
  int id = 0;
  if (parse_numeric_expr(expr, &id))
    return id;
  if (parse_numeric_expr(value_expr, &id))
    return id;
  return doc ? doc->next_id++ : CTRL_ID_BASE;
}

static bool frame_attr(xmlNodePtr node, irect16_t *out) {
  if (!out) return false;
  char *v = xml_attr_dup(node, "frame");
  if (!v) return false;
  int x = 0, y = 0, w = 0, h = 0;
  bool ok = sscanf(v, "%d %d %d %d", &x, &y, &w, &h) == 4;
  free(v);
  if (!ok) return false;
  *out = (irect16_t){x, y, w, h};
  return true;
}

static void project_reset(void) {
  if (!g_app) return;
  while (g_app->docs)
    close_form_doc(g_app->docs);
  memset(&g_app->project, 0, sizeof(g_app->project));
}

static void project_load_plugins(xmlNodePtr root) {
  if (!g_app) return;
  for (xmlNodePtr n = root->children; n; n = n->next) {
    if (n->type != XML_ELEMENT_NODE || xmlStrcmp(n->name, BAD_CAST "plugins") != 0)
      continue;
    for (xmlNodePtr p = n->children; p; p = p->next) {
      if (p->type != XML_ELEMENT_NODE || xmlStrcmp(p->name, BAD_CAST "plugin") != 0)
        continue;
      if (g_app->project.plugin_count >= FE_MAX_PROJECT_PLUGINS) continue;
      form_plugin_ref_t *ref = &g_app->project.plugins[g_app->project.plugin_count];
      copy_attr(p, "name", ref->name, sizeof(ref->name));
      if (ref->name[0]) {
        load_component_plugin_named(ref->name);
        g_app->project.plugin_count++;
      }
    }
  }
}

static void project_load_menus(xmlDocPtr xdoc, xmlNodePtr root) {
  if (!g_app) return;
  g_app->project.menus_xml[0] = '\0';
  for (xmlNodePtr n = root->children; n; n = n->next) {
    if (n->type != XML_ELEMENT_NODE || xmlStrcmp(n->name, BAD_CAST "menus") != 0)
      continue;
    xmlBufferPtr buf = xmlBufferCreate();
    if (!buf) return;
    int ok = xmlNodeDump(buf, xdoc, n, 4, 1);
    if (ok >= 0) {
      snprintf(g_app->project.menus_xml, sizeof(g_app->project.menus_xml),
               "%s", (const char *)xmlBufferContent(buf));
    }
    xmlBufferFree(buf);
    return;
  }
}

static void project_load_controls(form_doc_t *doc, xmlNodePtr form_node) {
  for (xmlNodePtr n = form_node->children; n; n = n->next) {
    if (n->type != XML_ELEMENT_NODE || xmlStrcmp(n->name, BAD_CAST "controls") != 0)
      continue;
    for (xmlNodePtr c = n->children; c; c = c->next) {
      if (c->type != XML_ELEMENT_NODE || xmlStrcmp(c->name, BAD_CAST "control") != 0)
        continue;
      if (doc->element_count >= MAX_ELEMENTS) break;

      char *class_name = xml_attr_dup(c, "class");
      int type = ctrl_type_from_token(class_name);
      free(class_name);
      if (type < 0 || type >= FE_MAX_COMPONENTS) continue;

      form_element_t *el = &doc->elements[doc->element_count++];
      memset(el, 0, sizeof(*el));
      el->type = type;
      copy_attr(c, "id", el->id_expr, sizeof(el->id_expr));
      char value_expr[32] = {0};
      copy_attr(c, "value", value_expr, sizeof(value_expr));
      el->id = project_resolve_control_id(doc, el->id_expr, value_expr);
      if (!frame_attr(c, &el->frame)) {
        el->frame.x = int_attr(c, "x", 0);
        el->frame.y = int_attr(c, "y", 0);
        el->frame.w = int_attr(c, "w", 10);
        el->frame.h = int_attr(c, "h", 8);
      }
      el->frame.w = MAX(1, el->frame.w);
      el->frame.h = MAX(1, el->frame.h);
      copy_attr(c, "flags", el->flags_expr, sizeof(el->flags_expr));
      el->flags = parse_flags_expr(el->flags_expr);
      copy_attr(c, "text", el->text, sizeof(el->text));
      copy_attr(c, "name", el->name, sizeof(el->name));
      if (!el->name[0])
        snprintf(el->name, sizeof(el->name), "control%d", doc->element_count);
      if (el->id >= doc->next_id)
        doc->next_id = el->id + 1;
      if (type >= 0 && type < FE_MAX_COMPONENTS)
        doc->type_counters[type]++;
    }
  }
}

static void project_load_requires(form_doc_t *doc, xmlNodePtr form_node) {
  for (xmlNodePtr n = form_node->children; n; n = n->next) {
    if (n->type != XML_ELEMENT_NODE || xmlStrcmp(n->name, BAD_CAST "requires") != 0)
      continue;
    copy_attr(n, "library", doc->required_plugin, sizeof(doc->required_plugin));
    return;
  }
}

static bool project_load_form_node(xmlNodePtr form_node) {
  int w = int_attr(form_node, "width", FORM_DEFAULT_W);
  int h = int_attr(form_node, "height", FORM_DEFAULT_H);
  irect16_t frame = {0};
  if (frame_attr(form_node, &frame)) {
    w = MAX(1, frame.w);
    h = MAX(1, frame.h);
  }
  form_doc_t *doc = create_form_doc(w, h);
  if (!doc) return false;

  copy_attr(form_node, "id", doc->form_id, sizeof(doc->form_id));
  copy_attr(form_node, "title", doc->form_title, sizeof(doc->form_title));
  copy_attr(form_node, "owner", doc->owner, sizeof(doc->owner));
  char flags_expr[128] = {0};
  copy_attr(form_node, "flags", flags_expr, sizeof(flags_expr));
  doc->flags = parse_flags_expr(flags_expr);
  project_load_requires(doc, form_node);
  project_load_controls(doc, form_node);

  form_doc_apply_window_flags_and_size(doc);
  canvas_rebuild_live_controls(doc);
  doc->modified = false;
  form_doc_update_title(doc);
  return true;
}

static void project_load_forms(xmlNodePtr root) {
  for (xmlNodePtr n = root->children; n; n = n->next) {
    if (n->type != XML_ELEMENT_NODE || xmlStrcmp(n->name, BAD_CAST "forms") != 0)
      continue;
    for (xmlNodePtr f = n->children; f; f = f->next) {
      if (f->type == XML_ELEMENT_NODE && xmlStrcmp(f->name, BAD_CAST "form") == 0)
        project_load_form_node(f);
    }
  }
}

bool form_project_load(const char *path) {
  xmlDocPtr xdoc = xmlReadFile(path, NULL, XML_PARSE_NONET);
  if (!xdoc) return false;
  xmlNodePtr root = xmlDocGetRootElement(xdoc);
  if (!root || xmlStrcmp(root->name, BAD_CAST "orion") != 0) {
    xmlFreeDoc(xdoc);
    return false;
  }

  project_reset();
  snprintf(g_app->project.filename, sizeof(g_app->project.filename), "%s", path);
  copy_attr(root, "name", g_app->project.name, sizeof(g_app->project.name));
  copy_attr(root, "title", g_app->project.title, sizeof(g_app->project.title));
  copy_attr(root, "root", g_app->project.root, sizeof(g_app->project.root));

  project_load_plugins(root);
  project_load_menus(xdoc, root);
  formeditor_rebuild_tool_palette();
  project_load_forms(root);

  g_app->project.loaded = true;
  g_app->project.modified = false;
  if (g_app->docs) form_doc_show_only(g_app->docs);
  forms_browser_refresh();
  plugins_browser_refresh();
  xmlFreeDoc(xdoc);
  return true;
}

static void xml_write_escaped(FILE *f, const char *s) {
  for (const char *p = s ? s : ""; *p; p++) {
    switch (*p) {
      case '&':  fputs("&amp;", f); break;
      case '<':  fputs("&lt;", f); break;
      case '>':  fputs("&gt;", f); break;
      case '"':  fputs("&quot;", f); break;
      default:   fputc(*p, f); break;
    }
  }
}

static void xml_attr(FILE *f, const char *name, const char *value) {
  fprintf(f, " %s=\"", name);
  xml_write_escaped(f, value);
  fputc('"', f);
}

static void project_save_doc(FILE *f, form_doc_t *doc) {
  const char *label = doc->form_title[0] ? doc->form_title :
                      (doc->form_id[0] ? doc->form_id : "Untitled");
  fprintf(f, "      <form");
  xml_attr(f, "id", doc->form_id[0] ? doc->form_id : "form");
  xml_attr(f, "title", label);
  fprintf(f, "\n            frame=\"0 0 %d %d\"\n            flags=\"%" PRIu32 "\"",
          doc->form_size.w, doc->form_size.h, doc->flags);
  if (doc->owner[0]) xml_attr(f, "owner", doc->owner);
  fprintf(f, ">\n");

  if (doc->required_plugin[0]) {
    fprintf(f, "        <requires");
    xml_attr(f, "library", doc->required_plugin);
    fprintf(f, " />\n");
  }
  fprintf(f, "        <controls>\n");
  for (int i = 0; i < doc->element_count; i++) {
    form_element_t *el = &doc->elements[i];
    fprintf(f, "          <control");
    xml_attr(f, "class", ctrl_type_token(el->type));
    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "%d", el->id);
    xml_attr(f, "id", el->id_expr[0] ? el->id_expr : id_buf);
    if (el->id_expr[0] && !is_numeric_expr(el->id_expr))
      xml_attr(f, "value", id_buf);
    xml_attr(f, "name", el->name);
    xml_attr(f, "text", el->text);
    fprintf(f, " frame=\"%d %d %d %d\"",
            el->frame.x, el->frame.y, el->frame.w, el->frame.h);
    char flags_buf[32];
    snprintf(flags_buf, sizeof(flags_buf), "%" PRIu32, el->flags);
    xml_attr(f, "flags", el->flags_expr[0] ? el->flags_expr : flags_buf);
    fprintf(f, " />\n");
  }
  fprintf(f, "        </controls>\n");
  fprintf(f, "      </form>\n");
}

bool form_project_save(const char *path) {
  if (!g_app) return false;
  FILE *f = fopen(path, "w");
  if (!f) return false;
  form_project_t *p = &g_app->project;

  fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
  fprintf(f, "<orion version=\"1\"");
  if (p->name[0]) xml_attr(f, "name", p->name);
  if (p->title[0]) xml_attr(f, "title", p->title);
  if (p->root[0]) xml_attr(f, "root", p->root);
  fprintf(f, ">\n\n");

  fprintf(f, "    <plugins>\n");
  for (int i = 0; i < p->plugin_count; i++) {
    fprintf(f, "      <plugin");
    xml_attr(f, "name", p->plugins[i].name);
    fprintf(f, " />\n");
  }
  fprintf(f, "    </plugins>\n\n");

  if (p->menus_xml[0]) {
    fprintf(f, "%s\n\n", p->menus_xml);
  }

  fprintf(f, "    <forms>\n");
  for (form_doc_t *doc = g_app->docs; doc; doc = doc->next)
    project_save_doc(f, doc);
  fprintf(f, "    </forms>\n");
  fprintf(f, "</orion>\n");

  fclose(f);
  snprintf(p->filename, sizeof(p->filename), "%s", path);
  p->loaded = true;
  p->modified = false;
  for (form_doc_t *doc = g_app->docs; doc; doc = doc->next) {
    doc->modified = false;
    form_doc_update_title(doc);
  }
  return true;
}

// ============================================================
// About dialog
// ============================================================

#define ABOUT_W 220
#define ABOUT_H  80

static result_t about_proc(window_t *win, uint32_t msg,
                            uint32_t wparam, void *lparam) {
  (void)lparam;
  switch (msg) {
    case evCreate: {
      // Info labels
      create_window("Orion Form Editor", WINDOW_NOTITLE | WINDOW_NOFILL,
          MAKERECT(8, 8, ABOUT_W - 16, CONTROL_HEIGHT),
          win, "label", 0, NULL);
      create_window("Version 1.0", WINDOW_NOTITLE | WINDOW_NOFILL,
          MAKERECT(8, 22, ABOUT_W - 16, CONTROL_HEIGHT),
          win, "label", 0, (void *)(uintptr_t)brTextDisabled);
      create_window("VB3-inspired form designer", WINDOW_NOTITLE | WINDOW_NOFILL,
          MAKERECT(8, 36, ABOUT_W - 16, CONTROL_HEIGHT),
          win, "label", 0, (void *)(uintptr_t)brTextDisabled);
      // OK button
      create_window("OK", BUTTON_DEFAULT,
          MAKERECT(ABOUT_W - 54, ABOUT_H - BUTTON_HEIGHT - 4, 50, BUTTON_HEIGHT),
          win, "button", 0, NULL);
      return true;
    }
    case evCommand:
      if (HIWORD(wparam) == btnClicked) {
        end_dialog(win, 1);
        return true;
      }
      return false;
    default:
      return false;
  }
}

void show_about_dialog(window_t *parent) {
  show_dialog("About Orion Form Editor",
              ABOUT_W, ABOUT_H + TITLEBAR_HEIGHT,
              parent, about_proc, NULL);
}

// ============================================================
// Grid Settings dialog
// ============================================================

#define GRID_W   180
#define GRID_H   108

#define GRID_ROW1_Y   6
#define GRID_ROW2_Y   (GRID_ROW1_Y + BUTTON_HEIGHT + 4)
#define GRID_ROW3_Y   (GRID_ROW2_Y + BUTTON_HEIGHT + 4)
#define GRID_BTN_Y    (GRID_H - BUTTON_HEIGHT - 6)

#define GRID_ID_SHOW   1
#define GRID_ID_SNAP   2
#define GRID_ID_SIZE   3
#define GRID_ID_OK     4
#define GRID_ID_CANCEL 5

#define GRID_SIZE_MIN  1
#define GRID_SIZE_MAX  64

// grid_size is bound via DDX_TEXT; checkboxes are handled manually.
typedef struct {
  int  grid_size;
} grid_size_data_t;

static const form_ctrl_def_t kGridChildren[] = {
  { "checkbox", GRID_ID_SHOW, {4,        GRID_ROW1_Y, GRID_W-8, BUTTON_HEIGHT}, 0,             "Show grid",    "chk_show"   },
  { "checkbox", GRID_ID_SNAP, {4,        GRID_ROW2_Y, GRID_W-8, BUTTON_HEIGHT}, 0,             "Snap to grid", "chk_snap"   },
  { "label",    -1,           {4,        GRID_ROW3_Y, 60,       CONTROL_HEIGHT}, 0,             "Grid size:",   "lbl_size"   },
  { "textedit", GRID_ID_SIZE, {68,       GRID_ROW3_Y, 40,       BUTTON_HEIGHT},  0,             "",             "edit_size"  },
  { "button",   GRID_ID_OK,     {GRID_W-108, GRID_BTN_Y, 50, BUTTON_HEIGHT}, BUTTON_DEFAULT, "OK",     "btn_ok"     },
  { "button",   GRID_ID_CANCEL, {GRID_W-54,  GRID_BTN_Y, 50, BUTTON_HEIGHT}, 0,             "Cancel", "btn_cancel" },
};

static const ctrl_binding_t k_grid_bindings[] = {
  DDX_TEXT(GRID_ID_SIZE, grid_size_data_t, grid_size),
};

static const form_def_t kGridForm = {
  .name          = "Grid Settings",
  .width         = GRID_W,
  .height        = GRID_H,
  .flags         = 0,
  .children      = kGridChildren,
  .child_count   = ARRAY_LEN(kGridChildren),
  .bindings      = k_grid_bindings,
  .binding_count = ARRAY_LEN(k_grid_bindings),
  .ok_id         = GRID_ID_OK,
  .cancel_id     = GRID_ID_CANCEL,
};

typedef struct {
  form_doc_t *doc;
  bool        accepted;
} grid_dlg_state_t;

static result_t grid_dlg_proc(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam) {
  grid_dlg_state_t *gs = (grid_dlg_state_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      gs = (grid_dlg_state_t *)lparam;
      win->userdata = gs;
      form_doc_t *doc = gs->doc;
      // Set checkbox states manually (no checkbox DDX helper yet).
      window_t *chk_show = get_window_item(win, GRID_ID_SHOW);
      window_t *chk_snap = get_window_item(win, GRID_ID_SNAP);
      if (chk_show)
        send_message(chk_show, btnSetCheck,
                     doc->show_grid ? btnStateChecked : btnStateUnchecked, NULL);
      if (chk_snap)
        send_message(chk_snap, btnSetCheck,
                     doc->snap_to_grid ? btnStateChecked : btnStateUnchecked, NULL);
      // Push grid_size via DDX.
      grid_size_data_t gsd = { doc->grid_size };
      dialog_push(win, &gsd, k_grid_bindings, ARRAY_LEN(k_grid_bindings));
      return true;
    }
    case evCommand: {
      if (HIWORD(wparam) != btnClicked) return false;
      window_t *src = (window_t *)lparam;
      if (!src) return false;
      if (src->id == GRID_ID_OK) {
        form_doc_t *doc = gs->doc;
        // Pull grid_size via DDX.
        grid_size_data_t gsd = { doc->grid_size };
        dialog_pull(win, &gsd, k_grid_bindings, ARRAY_LEN(k_grid_bindings));
        if (gsd.grid_size < GRID_SIZE_MIN) gsd.grid_size = GRID_SIZE_MIN;
        if (gsd.grid_size > GRID_SIZE_MAX) gsd.grid_size = GRID_SIZE_MAX;
        doc->grid_size = gsd.grid_size;
        // Pull checkboxes manually.
        window_t *chk_show = get_window_item(win, GRID_ID_SHOW);
        window_t *chk_snap = get_window_item(win, GRID_ID_SNAP);
        if (chk_show)
          doc->show_grid = (send_message(chk_show, btnGetCheck, 0, NULL) == btnStateChecked);
        if (chk_snap)
          doc->snap_to_grid = (send_message(chk_snap, btnGetCheck, 0, NULL) == btnStateChecked);
        gs->accepted = true;
        end_dialog(win, 1);
        return true;
      }
      if (src->id == GRID_ID_CANCEL) {
        end_dialog(win, 0);
        return true;
      }
      return false;
    }
    default:
      return false;
  }
}

static void show_grid_settings_dialog(window_t *parent, form_doc_t *doc) {
  grid_dlg_state_t gs = { doc, false };
  show_dialog_from_form(&kGridForm, "Grid Settings", parent, grid_dlg_proc, &gs);
  if (gs.accepted && doc->canvas_win)
    invalidate_window(doc->canvas_win);
}

// ============================================================
// Properties dialog
// ============================================================

#define PROPS_W  260
#define PROPS_H  110

// Child IDs
#define PROPS_ID_CAPTION   1
#define PROPS_ID_NAME      2
#define PROPS_ID_OK        3
#define PROPS_ID_CANCEL    4

// Computed row positions (mirrors the form below)
#define PROPS_ROW1_Y       4
#define PROPS_ROW2_Y       (PROPS_ROW1_Y + BUTTON_HEIGHT + 6)   // 23
#define PROPS_INFO_Y       (PROPS_ROW2_Y + BUTTON_HEIGHT + 6)   // 42
#define PROPS_BTN_Y        (PROPS_H - BUTTON_HEIGHT - 6)        // 86

static const form_ctrl_def_t kPropsChildren[] = {
  { "label",    -1,              {4,          PROPS_ROW1_Y, 60,           CONTROL_HEIGHT}, 0,             "Caption:", "lbl_caption" },
  { "textedit", PROPS_ID_CAPTION,{68,         PROPS_ROW1_Y, PROPS_W - 72, BUTTON_HEIGHT},  0,             "",         "edit_caption"},
  { "label",    -1,              {4,          PROPS_ROW2_Y, 60,           CONTROL_HEIGHT}, 0,             "Name:",    "lbl_name"    },
  { "textedit", PROPS_ID_NAME,   {68,         PROPS_ROW2_Y, PROPS_W - 72, BUTTON_HEIGHT},  0,             "",         "edit_name"   },
  { "button",   PROPS_ID_OK,     {PROPS_W-108, PROPS_BTN_Y, 50,           BUTTON_HEIGHT},  BUTTON_DEFAULT, "OK",      "btn_ok"      },
  { "button",   PROPS_ID_CANCEL, {PROPS_W-54,  PROPS_BTN_Y, 50,           BUTTON_HEIGHT},  0,             "Cancel",   "btn_cancel"  },
};

// DDX bindings: caption and name edits ↔ form_element_t.text / .name
static const ctrl_binding_t k_props_bindings[] = {
  DDX_TEXT(PROPS_ID_CAPTION, form_element_t, text),
  DDX_TEXT(PROPS_ID_NAME, form_element_t, name),
};

static const form_def_t kPropsForm = {
  .name          = "Element Properties",
  .width         = PROPS_W,
  .height        = PROPS_H,
  .flags         = 0,
  .children      = kPropsChildren,
  .child_count   = ARRAY_LEN(kPropsChildren),
  .bindings      = k_props_bindings,
  .binding_count = ARRAY_LEN(k_props_bindings),
  .ok_id         = PROPS_ID_OK,
  .cancel_id     = PROPS_ID_CANCEL,
};

typedef struct {
  form_element_t *el;
  bool            accepted;
} props_state_t;

static result_t props_proc(window_t *win, uint32_t msg,
                            uint32_t wparam, void *lparam) {
  props_state_t *ps = (props_state_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      ps = (props_state_t *)lparam;
      win->userdata = ps;

      // Dynamic type-info label (content is computed at runtime).
      char info[64];
      snprintf(info, sizeof(info), "Type: %s  ID: %d  (%d, %d)  %d x %d",
               ctrl_type_token(ps->el->type), ps->el->id,
               ps->el->frame.x, ps->el->frame.y, ps->el->frame.w, ps->el->frame.h);
      create_window(info, WINDOW_NOTITLE | WINDOW_NOFILL,
          MAKERECT(4, PROPS_INFO_Y, PROPS_W - 8, CONTROL_HEIGHT),
          win, "label", 0, (void *)(uintptr_t)brTextDisabled);

      // Pre-populate caption/name edits from the element.
      dialog_push(win, ps->el, k_props_bindings, ARRAY_LEN(k_props_bindings));
      return true;
    }

    case evCommand: {
      if (HIWORD(wparam) != btnClicked) return false;
      window_t *src = (window_t *)lparam;
      if (!src) return false;

      if (src->id == PROPS_ID_OK) {
        dialog_pull(win, ps->el, k_props_bindings, ARRAY_LEN(k_props_bindings));
        ps->accepted = true;
        end_dialog(win, 1);
        return true;
      }
      if (src->id == PROPS_ID_CANCEL) {
        end_dialog(win, 0);
        return true;
      }
      return false;
    }
    default:
      return false;
  }
}

bool show_props_dialog(window_t *parent, form_element_t *el) {
  props_state_t ps = {0};
  ps.el       = el;
  ps.accepted = false;
  show_dialog_from_form(&kPropsForm, "Element Properties", parent, props_proc, &ps);
  return ps.accepted;
}

// ============================================================
// File-picker wrapper (analogous to imageeditor/filepicker.c)
// ============================================================

static bool show_form_file_picker(window_t *parent, bool save_mode,
                                   char *out_path, size_t out_sz) {
  openfilename_t ofn = {0};
  ofn.lStructSize  = sizeof(ofn);
  ofn.hwndOwner    = parent;
  ofn.lpstrFile    = out_path;
  ofn.nMaxFile     = (uint32_t)out_sz;
  ofn.lpstrFilter  = "Orion Projects\0*.orion\0All Files\0*.*\0";
  ofn.nFilterIndex = 1;
  ofn.Flags        = save_mode ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST;
  return save_mode ? get_save_filename(&ofn) : get_open_filename(&ofn);
}

// ============================================================
// Menu command handler
// ============================================================

void handle_menu_command(uint16_t id) {
  if (!g_app) return;
  form_doc_t *doc = g_app->doc;

  switch (id) {
    case ID_FILE_NEW:
      create_form_doc(FORM_DEFAULT_W, FORM_DEFAULT_H);
      break;

    case ID_FILE_OPEN: {
      char path[512] = {0};
      window_t *owner = doc ? doc->doc_win : (g_app->menubar_win);
      if (show_form_file_picker(owner, false, path, sizeof(path))) {
        if (!form_project_load(path) && owner)
          message_box(owner, "Failed to load Orion project.", "Open", MB_OK);
      }
      break;
    }

    case ID_FILE_SAVE:
      if (g_app->project.loaded && g_app->project.filename[0]) {
        if (form_project_save(g_app->project.filename)) {
          if (doc && doc->doc_win)
            send_message(doc->doc_win, evStatusBar, 0, (void *)"Project saved");
        } else if (doc && doc->doc_win) {
          send_message(doc->doc_win, evStatusBar, 0, (void *)"Project save failed");
        }
      } else {
        goto do_save_as;
      }
      break;

    do_save_as:
    case ID_FILE_SAVEAS: {
      if (!doc && !g_app->docs) break;
      char path[512] = {0};
      window_t *owner = doc ? doc->doc_win : g_app->menubar_win;
      if (show_form_file_picker(owner, true, path, sizeof(path))) {
        if (form_project_save(path)) {
          if (doc && doc->doc_win)
            send_message(doc->doc_win, evStatusBar, 0, path);
        } else {
          if (doc && doc->doc_win)
            send_message(doc->doc_win, evStatusBar, 0, (void *)"Project save failed");
        }
      }
      break;
    }

    case ID_FILE_QUIT:
#ifdef BUILD_AS_GEM
      if (g_app) {
        while (g_app->docs)
          close_form_doc(g_app->docs);
        if (g_app->tool_win)    destroy_window(g_app->tool_win);
        if (g_app->menubar_win) destroy_window(g_app->menubar_win);
      }
#else
      ui_request_quit();
#endif
      break;

    case ID_EDIT_DELETE: {
      if (!doc) break;
      window_t *cwin = doc->canvas_win;
      if (!cwin) break;
      canvas_state_t *cs = (canvas_state_t *)cwin->userdata;
      if (!cs || cs->selected_idx < 0) break;
      int idx = cs->selected_idx;
      if (doc->elements[idx].live_win)
        destroy_window(doc->elements[idx].live_win);
      // Remove element by shifting the array
      for (int i = idx; i < doc->element_count - 1; i++)
        doc->elements[i] = doc->elements[i + 1];
      doc->element_count--;
      cs->selected_idx = -1;
      doc->modified = true;
      form_doc_update_title(doc);
      canvas_rebuild_live_controls(doc);
      break;
    }

    case ID_EDIT_PROPS: {
      if (!doc) break;
      window_t *cwin = doc->canvas_win;
      if (!cwin) break;
      canvas_state_t *cs = (canvas_state_t *)cwin->userdata;
      if (!cs || cs->selected_idx < 0) break;
      form_element_t *el = &doc->elements[cs->selected_idx];
      window_t *owner = g_app->menubar_win ? g_app->menubar_win : doc->doc_win;
      if (show_props_dialog(owner, el)) {
        doc->modified = true;
        form_doc_update_title(doc);
        canvas_sync_live_controls(doc);
        property_browser_refresh(doc);
      }
      break;
    }

    case ID_VIEW_GRID: {
      if (!doc) break;
      window_t *owner = g_app->menubar_win ? g_app->menubar_win : doc->doc_win;
      show_grid_settings_dialog(owner, doc);
      break;
    }

    case ID_HELP_ABOUT: {
      window_t *owner = g_app->menubar_win ? g_app->menubar_win : (doc ? doc->doc_win : NULL);
      show_about_dialog(owner);
      break;
    }

    default:
      if (id != ID_TOOL_SELECT && fe_component_by_tool_ident(id))
        break;
      break;
  }
}

// ============================================================
// Menu bar window procedure
// ============================================================

result_t editor_menubar_proc(window_t *win, uint32_t msg,
                              uint32_t wparam, void *lparam) {
  if (msg == evCommand) {
    uint16_t notif = HIWORD(wparam);
    if (notif == kMenuBarNotificationItemClick ||
        notif == kAcceleratorNotification      ||
        notif == btnClicked) {
      handle_menu_command(LOWORD(wparam));
      return true;
    }
  }
  return win_menubar(win, msg, wparam, lparam);
}
