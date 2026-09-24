// Minimal project XML I/O for window-first form runtime.

#include "formeditor.h"
#include <orion/user/icons.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

extern app_state_t *g_app;
extern void forms_browser_refresh(void);
extern void plugins_browser_refresh(void);
extern void form_doc_show_only(window_t *doc);
extern window_t *create_form_doc(int w, int h);
extern void close_form_doc(window_t *doc);
extern void form_doc_update_title(window_t *doc);
extern void canvas_rebuild_live_controls(window_t *doc);

static int feio_xml_attr_int(xmlNodePtr node, const char *name, int fallback) {
  xmlChar *v = xmlGetProp(node, BAD_CAST name);
  if (!v)
    return fallback;
  char *end = NULL;
  long n = strtol((const char *)v, &end, 0);
  bool ok = (end && *end == '\0');
  xmlFree(v);
  if (!ok)
    return fallback;
  return (int)n;
}

static void feio_xml_attr_copy(xmlNodePtr node, const char *name, char *dst, size_t dst_sz) {
  if (!dst || dst_sz == 0)
    return;
  dst[0] = '\0';
  xmlChar *v = xmlGetProp(node, BAD_CAST name);
  if (!v)
    return;
  snprintf(dst, dst_sz, "%s", (const char *)v);
  xmlFree(v);
}

static bool feio_xml_elem(xmlNodePtr node, const char *name) {
  return node && node->type == XML_ELEMENT_NODE &&
         xmlStrcasecmp(node->name, BAD_CAST name) == 0;
}

static xmlNodePtr feio_first_child_named(xmlNodePtr parent, const char *name) {
  for (xmlNodePtr n = parent ? parent->children : NULL; n; n = n->next) {
    if (feio_xml_elem(n, name))
      return n;
  }
  return NULL;
}

static void feio_xml_set_prop_int(xmlNodePtr node, const char *name, int value) {
  if (!node || !name)
    return;
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", value);
  xmlSetProp(node, BAD_CAST name, BAD_CAST buf);
}

static void feio_clear_children(xmlNodePtr node) {
  while (node && node->children) {
    xmlNodePtr child = node->children;
    xmlUnlinkNode(child);
    xmlFreeNode(child);
  }
}

static int feio_doc_form_width(const window_t *doc) {
  return (doc && doc->children && doc->children->frame.w > 0)
           ? doc->children->frame.w
           : FORM_DEFAULT_W;
}

static int feio_doc_form_height(const window_t *doc) {
  return (doc && doc->children && doc->children->frame.h > 0)
           ? doc->children->frame.h
           : FORM_DEFAULT_H;
}

static void feio_doc_clean_title(const window_t *doc, char *out, size_t out_sz) {
  if (!out || out_sz == 0)
    return;
  snprintf(out, out_sz, "%s", (doc && doc->title[0]) ? doc->title : "Form");
  size_t len = strlen(out);
  if (len >= 2 && strcmp(out + len - 2, " *") == 0)
    out[len - 2] = '\0';
}

static xmlNodePtr feio_ensure_doc_form_node(window_t *doc) {
  if (!doc)
    return NULL;
  xmlNodePtr form = (xmlNodePtr)doc->userdata2;
  if (feio_xml_elem(form, "form"))
    return form;

  form = xmlNewNode(NULL, BAD_CAST "form");
  if (!form)
    return NULL;

  char title[512];
  feio_doc_clean_title(doc, title, sizeof(title));
  xmlSetProp(form, BAD_CAST "name", BAD_CAST (title[0] ? title : "Form"));
  xmlSetProp(form, BAD_CAST "title", BAD_CAST (title[0] ? title : "Form"));
  feio_xml_set_prop_int(form, "width", feio_doc_form_width(doc));
  feio_xml_set_prop_int(form, "height", feio_doc_form_height(doc));
  doc->userdata2 = form;
  return form;
}

static xmlNodePtr feio_find_runtime_node_by_id(xmlNodePtr parent,
                                               uint32_t target_id,
                                               uint32_t *next_id) {
  if (!parent || !next_id)
    return NULL;

  for (xmlNodePtr node = parent->children; node; node = node->next) {
    if (node->type != XML_ELEMENT_NODE)
      continue;

    if (feio_xml_elem(node, "Column") &&
        (feio_xml_elem(node->parent, "TableView") || feio_xml_elem(node->parent, "ReportView")))
      continue;

    uint32_t id = (uint32_t)feio_xml_attr_int(node, "id", 0);
    if (!id)
      id = (*next_id)++;
    if (id == target_id)
      return node;

    xmlNodePtr found = feio_find_runtime_node_by_id(node, target_id, next_id);
    if (found)
      return found;
  }

  return NULL;
}

static xmlNodePtr feio_xml_node_for_window(window_t *doc, window_t *target) {
  xmlNodePtr form = (xmlNodePtr)(doc ? doc->userdata2 : NULL);
  if (!feio_xml_elem(form, "form") || !target)
    return NULL;
  if (target == doc || target == doc->children)
    return form;

  uint32_t next_id = CTRL_ID_BASE;
  return feio_find_runtime_node_by_id(form, target->id, &next_id);
}

static void feio_update_form_save_attrs(xmlNodePtr form, const window_t *doc) {
  if (!form || !doc)
    return;

  char title[512];
  feio_doc_clean_title(doc, title, sizeof(title));
  if (!title[0])
    snprintf(title, sizeof(title), "Form");

  if (!xmlHasProp(form, BAD_CAST "name"))
    xmlSetProp(form, BAD_CAST "name", BAD_CAST title);
  xmlSetProp(form, BAD_CAST "title", BAD_CAST title);
  feio_xml_set_prop_int(form, "width", feio_doc_form_width(doc));
  feio_xml_set_prop_int(form, "height", feio_doc_form_height(doc));
}

static xmlNodePtr feio_copy_form_for_save(const window_t *doc) {
  if (!doc)
    return NULL;

  xmlNodePtr form = feio_xml_elem((xmlNodePtr)doc->userdata2, "form")
                      ? xmlCopyNode((xmlNodePtr)doc->userdata2, 1)
                      : NULL;
  if (!form) {
    form = xmlNewNode(NULL, BAD_CAST "form");
    if (!form)
      return NULL;
  }
  feio_update_form_save_attrs(form, doc);
  return form;
}

static xmlNodePtr feio_project_root_for_save(void) {
  xmlNodePtr root = g_app && g_app->project.xml_root
                      ? xmlCopyNode((xmlNodePtr)g_app->project.xml_root, 1)
                      : NULL;
  if (!root)
    root = xmlNewNode(NULL, BAD_CAST "orion");
  if (!root)
    return NULL;

  xmlSetProp(root, BAD_CAST "version", BAD_CAST "1");
  xmlSetProp(root, BAD_CAST "name",
             BAD_CAST (g_app->project.name[0] ? g_app->project.name : "project"));
  xmlSetProp(root, BAD_CAST "title",
             BAD_CAST (g_app->project.title[0] ? g_app->project.title : "Orion Project"));
  xmlSetProp(root, BAD_CAST "root",
             BAD_CAST (g_app->project.root[0] ? g_app->project.root : "./"));
  return root;
}

void fe_project_clear_xml(void) {
  if (!g_app || !g_app->project.xml_root)
    return;
  xmlFreeNode((xmlNodePtr)g_app->project.xml_root);
  g_app->project.xml_root = NULL;
}

void fe_project_clear_doc_xml(window_t *doc) {
  if (!doc || !doc->userdata2)
    return;
  xmlFreeNode((xmlNodePtr)doc->userdata2);
  doc->userdata2 = NULL;
}

static void fe_close_all_docs(void) {
  if (!g_app)
    return;
  while (g_app->form_count > 0 && g_app->forms[0])
    close_form_doc(g_app->forms[0]);
  g_app->active_form = NULL;
}

static void fe_load_project_meta(xmlNodePtr root) {
  if (!g_app)
    return;
  g_app->project.name[0] = '\0';
  g_app->project.title[0] = '\0';
  g_app->project.root[0] = '\0';
  feio_xml_attr_copy(root, "name", g_app->project.name, sizeof(g_app->project.name));
  feio_xml_attr_copy(root, "title", g_app->project.title, sizeof(g_app->project.title));
  feio_xml_attr_copy(root, "root", g_app->project.root, sizeof(g_app->project.root));
}

static bool fe_plugin_name_in_refs(const form_plugin_ref_t *refs, int count,
                                   const char *name) {
  for (int i = 0; refs && i < count; i++)
    if (strcmp(refs[i].name, name) == 0) return true;
  return false;
}

static bool fe_load_plugin_named(const char *name) {
  if (!name || !*name) return false;
  if (strchr(name, '/') || strstr(name, AX_DYNLIB_EXT))
    return fe_load_component_plugin(name);
  char path[4096];
  int n = snprintf(path, sizeof(path), "%s/../lib/%s%s",
                   ui_get_exe_dir(), name, AX_DYNLIB_EXT);
  return n > 0 && (size_t)n < sizeof(path) && fe_load_component_plugin(path);
}

static bool fe_load_project_plugins(xmlNodePtr root) {
  if (!g_app || !root) return false;
  form_plugin_ref_t previous[FE_MAX_PROJECT_PLUGINS];
  int previous_count = g_app->project.plugin_count;
  memcpy(previous, g_app->project.plugins, sizeof(previous));
  memset(g_app->project.plugins, 0, sizeof(g_app->project.plugins));
  g_app->project.plugin_count = 0;

  for (xmlNodePtr group = root->children; group; group = group->next) {
    if (!feio_xml_elem(group, "plugins")) continue;
    for (xmlNodePtr node = group->children; node; node = node->next) {
      if (!feio_xml_elem(node, "plugin")) continue;
      char name[64] = {0};
      feio_xml_attr_copy(node, "name", name, sizeof(name));
      if (!name[0] || fe_plugin_name_in_refs(g_app->project.plugins,
                                             g_app->project.plugin_count, name))
        continue;
      if (g_app->project.plugin_count >= FE_MAX_PROJECT_PLUGINS) return false;
      if (!fe_plugin_name_in_refs(previous, previous_count, name) &&
          !fe_load_plugin_named(name))
        return false;
      snprintf(g_app->project.plugins[g_app->project.plugin_count++].name,
               sizeof(g_app->project.plugins[0].name), "%s", name);
    }
  }
  return true;
}

static const char *fe_parse_sysicon_name(const char *name) {
  if (!name || !*name) return NULL;
  // Old sysicon_* names → SVG base names
  if (strcmp(name, "sysicon_add") == 0)     return "plus";
  if (strcmp(name, "sysicon_delete") == 0)  return "trash";
  if (strcmp(name, "sysicon_heart") == 0)   return "heart";
  if (strcmp(name, "sysicon_comment") == 0) return "chat-bubble";
  if (strcmp(name, "sysicon_folder") == 0)  return "folder";
  if (strcmp(name, "sysicon_play") == 0)    return "play";
  // New-format bare SVG names — recognised set
  if (strcmp(name, "plus") == 0)        return "plus";
  if (strcmp(name, "trash") == 0)       return "trash";
  if (strcmp(name, "heart") == 0)       return "heart";
  if (strcmp(name, "chat-bubble-empty") == 0) return "chat-bubble-empty";
  if (strcmp(name, "folder") == 0)      return "folder";
  if (strcmp(name, "play") == 0)        return "play";
  if (strcmp(name, "undo") == 0)        return "undo";
  if (strcmp(name, "refresh") == 0)     return "refresh";
  if (strcmp(name, "edit-pencil") == 0) return "edit-pencil";
  if (strcmp(name, "arrow-up") == 0)    return "arrow-up";
  if (strcmp(name, "arrow-down") == 0)  return "arrow-down";
  return NULL;
}

static uint32_t fe_parse_form_chrome_flags(xmlNodePtr form_node) {
  char flags_expr[256] = {0};
  feio_xml_attr_copy(form_node, "flags", flags_expr, sizeof(flags_expr));

  char buf[256];
  snprintf(buf, sizeof(buf), "%s", flags_expr);
  uint32_t out = 0;
  char *save = NULL;
  for (char *tok = strtok_r(buf, ",|", &save); tok; tok = strtok_r(NULL, ",|", &save)) {
    while (*tok == ' ' || *tok == '\t' || *tok == '\n' || *tok == '\r')
      tok++;
    size_t n = strlen(tok);
    while (n > 0 && (tok[n - 1] == ' ' || tok[n - 1] == '\t' || tok[n - 1] == '\n' || tok[n - 1] == '\r'))
      tok[--n] = '\0';
    if (strcasecmp(tok, "toolbar") == 0 || strcasecmp(tok, "WINDOW_TOOLBAR") == 0)
      out |= WINDOW_TOOLBAR;
    else if (strcasecmp(tok, "statusbar") == 0 || strcasecmp(tok, "WINDOW_STATUSBAR") == 0)
      out |= WINDOW_STATUSBAR;
  }
  if (feio_first_child_named(form_node, "Toolbar"))
    out |= WINDOW_TOOLBAR;
  return out;
}

static int fe_build_toolbar_items(xmlNodePtr form_node, toolbar_item_t *items,
                                  int max_items) {
  if (!form_node || !items || max_items <= 0)
    return 0;

  xmlNodePtr toolbar_node = feio_first_child_named(form_node, "Toolbar");
  if (!toolbar_node)
    return 0;

  int count = 0;
  for (xmlNodePtr n = toolbar_node->children; n && count < max_items; n = n->next) {
    if (n->type != XML_ELEMENT_NODE)
      continue;
    if (xmlStrcasecmp(n->name, BAD_CAST "spacer") == 0) {
      items[count++] = (toolbar_item_t){ .type = TOOLBAR_ITEM_SPACER, .icon = NULL };
      continue;
    }
    if (xmlStrcasecmp(n->name, BAD_CAST "Button") != 0)
      continue;

    char icon_name[64] = {0};
    feio_xml_attr_copy(n, "icon", icon_name, sizeof(icon_name));
    items[count++] = (toolbar_item_t){
      .type = TOOLBAR_ITEM_BUTTON,
      .ident = 0,
      .icon = fe_parse_sysicon_name(icon_name),
      .w = 0,
      .flags = 0,
      .text = NULL,
    };
  }

  return count;
}

static void fe_apply_form_preview_chrome(window_t *doc, xmlNodePtr form_node,
                                         int form_w, int form_h) {
  if (!doc || !form_node)
    return;

  uint32_t chrome = fe_parse_form_chrome_flags(form_node);
  uint32_t old_bits = doc->flags & (WINDOW_TOOLBAR | WINDOW_STATUSBAR);
  uint32_t new_bits = chrome & (WINDOW_TOOLBAR | WINDOW_STATUSBAR);
  if (old_bits != new_bits) {
    doc->flags = (doc->flags & ~(WINDOW_TOOLBAR | WINDOW_STATUSBAR)) | new_bits;
    irect16_t fr = form_doc_frame_for_size(form_w, form_h, doc->flags);
    resize_window(doc, fr.w, fr.h);
  }

  if (doc->flags & WINDOW_TOOLBAR) {
    toolbar_item_t items[32];
    memset(items, 0, sizeof(items));
    int count = fe_build_toolbar_items(form_node, items, 32);
    if (count > 0)
      send_message(doc, tbSetItems, (uint32_t)count, items);
  }

  if (doc->flags & WINDOW_STATUSBAR)
    send_message(doc, evStatusBar, 0, (void *)"Preview");
}

xmlNodePtr fe_project_table_node_for_window(window_t *doc, window_t *target) {
  xmlNodePtr node = feio_xml_node_for_window(doc, target);
  return feio_xml_elem(node, "TableView") ? node : NULL;
}

bool fe_project_update_table_column_binding(window_t *doc, window_t *target, int column_index,
                                            const char *field_expr,
                                            const char *title) {
  if (!field_expr || !*field_expr || column_index < 0)
    return false;

  xmlNodePtr table = fe_project_table_node_for_window(doc, target);
  if (!table) return false;
  xmlNodePtr column = NULL;
  int index = 0;
  for (xmlNodePtr node = table->children; node; node = node->next) {
    if (!feio_xml_elem(node, "Column")) continue;
    if (index++ == column_index) { column = node; break; }
  }
  if (!column) return false;

  xmlSetProp(column, BAD_CAST "field", BAD_CAST field_expr);
  if (title && *title)
    xmlSetProp(column, BAD_CAST "title", BAD_CAST title);
  return true;
}

bool fe_project_append_component_node(window_t *doc, window_t *parent_target,
                                      window_t *child, const char *class_name) {
  if (!doc || !child || !class_name || !*class_name)
    return false;

  xmlNodePtr form = feio_ensure_doc_form_node(doc);
  if (!form)
    return false;

  xmlNodePtr parent_node = feio_xml_node_for_window(doc, parent_target);
  if (!parent_node)
    parent_node = form;

  xmlNodePtr node = xmlNewChild(parent_node, NULL, BAD_CAST class_name, NULL);
  if (!node)
    return false;

  char name_buf[128];
  snprintf(name_buf, sizeof(name_buf), "%s_%u", class_name, child->id);
  xmlSetProp(node, BAD_CAST "name", BAD_CAST name_buf);
  if (child->title[0])
    xmlSetProp(node, BAD_CAST "text", BAD_CAST child->title);
  feio_xml_set_prop_int(node, "id", (int)child->id);
  if (child->layout.layout_fixed_w > 0)
    feio_xml_set_prop_int(node, "width", child->layout.layout_fixed_w);
  if (child->layout.layout_fixed_h > 0)
    feio_xml_set_prop_int(node, "height", child->layout.layout_fixed_h);
  return true;
}

static void fe_load_databases(xmlNodePtr root) {
  if (!g_app || !root)
    return;

  memset(g_app->project.databases, 0, sizeof(g_app->project.databases));
  g_app->project.database_count = 0;

  for (xmlNodePtr n = root->children; n; n = n->next) {
    if (n->type != XML_ELEMENT_NODE)
      continue;
    if (xmlStrcasecmp(n->name, BAD_CAST "databases") != 0)
      continue;

    for (xmlNodePtr db_node = n->children; db_node; db_node = db_node->next) {
      if (db_node->type != XML_ELEMENT_NODE)
        continue;
      if (xmlStrcasecmp(db_node->name, BAD_CAST "database") != 0)
        continue;
      if (g_app->project.database_count >= FE_MAX_PROJECT_DATABASES)
        break;

      char db_name[64] = {0};
      feio_xml_attr_copy(db_node, "name", db_name, sizeof(db_name));
      if (!db_name[0])
        continue;

      db_t *db = get_database_by_name(db_name);
      if (!db)
        continue;
      g_app->project.databases[g_app->project.database_count++] = db;
    }
  }

  if (ui_get_database() == NULL) {
    db_t *db = get_database_by_name("db");
    if (!db && g_app->project.database_count > 0)
      db = g_app->project.databases[0];
    if (db)
      ui_set_database(db);
  }
}

static void fe_load_form_node(xmlNodePtr form_node) {
  xmlChar *title_x = xmlGetProp(form_node, BAD_CAST "title");
  xmlChar *frame_x = xmlGetProp(form_node, BAD_CAST "frame");
  xmlChar *width_x = xmlGetProp(form_node, BAD_CAST "width");
  xmlChar *height_x = xmlGetProp(form_node, BAD_CAST "height");

  int w = feio_xml_attr_int(form_node, "width", FORM_DEFAULT_W);
  int h = feio_xml_attr_int(form_node, "height", FORM_DEFAULT_H);
  if (w < 1) w = FORM_DEFAULT_W;
  if (h < 1) h = FORM_DEFAULT_H;

  window_t *doc = create_form_doc(w, h);
  if (!doc) {
    if (title_x) xmlFree(title_x);
    if (frame_x) xmlFree(frame_x);
    if (width_x) xmlFree(width_x);
    if (height_x) xmlFree(height_x);
    return;
  }

  if (doc->userdata2) {
    xmlFreeNode((xmlNodePtr)doc->userdata2);
    doc->userdata2 = NULL;
  }
  doc->userdata2 = xmlCopyNode(form_node, 1);

  feio_xml_attr_copy(form_node, "title", doc->title, sizeof(doc->title));
  if (!doc->title[0])
    feio_xml_attr_copy(form_node, "name", doc->title, sizeof(doc->title));
  // Keep form XML metadata on doc->userdata2 for the preview runtime builder.
  // The document shell window itself must remain editor-owned and not inherit
  // runtime form flags/layout (toolbar/statusbar/padding), otherwise preview
  // and editor chrome get mixed.

  form_doc_update_title(doc);
  fe_apply_form_preview_chrome(doc, form_node, w, h);
  canvas_rebuild_live_controls(doc);

  if (title_x) xmlFree(title_x);
  if (frame_x) xmlFree(frame_x);
  if (width_x) xmlFree(width_x);
  if (height_x) xmlFree(height_x);
}

bool fe_project_load(const char *path) {
  if (!path || !g_app)
    return false;

  xmlDocPtr xdoc = xmlReadFile(path, NULL, XML_PARSE_NOBLANKS);
  if (!xdoc) {
    return false;
  }

  xmlNodePtr root = xmlDocGetRootElement(xdoc);
  if (!root || xmlStrcasecmp(root->name, BAD_CAST "orion") != 0) {
    xmlFreeDoc(xdoc);
    return false;
  }

  if (!fe_load_project_plugins(root)) {
    xmlFreeDoc(xdoc);
    return false;
  }

  xmlNodePtr root_copy = xmlCopyNode(root, 1);
  if (!root_copy) {
    xmlFreeDoc(xdoc);
    return false;
  }

  snprintf(g_app->project.filename, sizeof(g_app->project.filename), "%s", path);
  fe_close_all_docs();
  fe_project_clear_xml();
  g_app->project.xml_root = root_copy;
  fe_load_project_meta(root);
  fe_load_databases(root);

  for (xmlNodePtr n = root->children; n; n = n->next) {
    if (n->type != XML_ELEMENT_NODE)
      continue;
    if (xmlStrcasecmp(n->name, BAD_CAST "forms") != 0)
      continue;

    for (xmlNodePtr f = n->children; f; f = f->next) {
      if (f->type != XML_ELEMENT_NODE)
        continue;
      if (xmlStrcasecmp(f->name, BAD_CAST "form") == 0)
        fe_load_form_node(f);
    }
  }

  if (g_app->form_count > 0 && g_app->forms[0]) {
    form_doc_show_only(g_app->forms[0]);
  }

  g_app->project.loaded = true;
  g_app->project.modified = false;
  forms_browser_refresh();
  plugins_browser_refresh();

  xmlFreeDoc(xdoc);
  return true;
}

bool fe_project_save(const char *path) {
  if (!g_app)
    return false;

  const char *out = path && *path ? path : g_app->project.filename;
  if (!out || !*out)
    return false;

  xmlDocPtr xdoc = xmlNewDoc(BAD_CAST "1.0");
  if (!xdoc)
    return false;

  xmlNodePtr root = feio_project_root_for_save();
  if (!root) {
    xmlFreeDoc(xdoc);
    return false;
  }
  xmlDocSetRootElement(xdoc, root);

  xmlNodePtr forms = feio_first_child_named(root, "forms");
  if (!forms)
    forms = xmlNewChild(root, NULL, BAD_CAST "forms", NULL);
  if (!forms) {
    xmlFreeDoc(xdoc);
    return false;
  }
  feio_clear_children(forms);

  for (int i = 0; i < g_app->form_count; i++) {
    xmlNodePtr form = feio_copy_form_for_save(g_app->forms[i]);
    if (form)
      xmlAddChild(forms, form);
  }

  int rc = xmlSaveFormatFileEnc(out, xdoc, "UTF-8", 1);
  if (rc < 0) {
    xmlFreeDoc(xdoc);
    return false;
  }

  fe_project_clear_xml();
  g_app->project.xml_root = xmlCopyNode(root, 1);
  xmlFreeDoc(xdoc);

  snprintf(g_app->project.filename, sizeof(g_app->project.filename), "%s", out);
  g_app->project.loaded = true;
  g_app->project.modified = false;
  return true;
}
