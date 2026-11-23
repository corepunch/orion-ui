#include "help.h"
#include "commctl.h"
#include "form_loader.h"
#include <orion/user/font_sdf.h>
#include <orion/user/i18n.h>
#include <orion/user/zip.h>

#include <libxml/parser.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HELP_BOOK_LIMIT     (16 * 1024 * 1024)
#define HELP_XML_LIMIT      (512 * 1024)
#define HELP_IMAGE_LIMIT    (4 * 1024 * 1024)
#define HELP_TOPIC_LIMIT    128
#define HELP_HISTORY_LIMIT  128
#define HELP_BLOCK_LIMIT    64

typedef struct {
  uint8_t *bytes;
  zip_t *zip;
  xmlDocPtr xml;
  xmlNodePtr root;
  xmlNodePtr topics[HELP_TOPIC_LIMIT];
  int count;
} help_book_t;

typedef struct {
  char path[4096];
  help_book_t book;
  form_library_t *form;
  window_t *back, *forward, *search, *topics, *no_results, *article;
  int history[HELP_HISTORY_LIMIT];
  int length, position;
} help_viewer_t;

static bool help_element(xmlNodePtr node, const char *name) {
  return node && node->type == XML_ELEMENT_NODE && xmlStrEqual(node->name, BAD_CAST name);
}

static const char *help_attr(xmlNodePtr node, const char *name) {
  for (xmlAttrPtr a = node->properties; a; a = a->next)
    if (!a->ns && xmlStrEqual(a->name, BAD_CAST name) && a->children && !a->children->next)
      return (const char *)a->children->content;
  return "";
}

static int help_topic(const help_book_t *book, const char *id) {
  for (int i = 0; i < book->count; i++)
    if (strcmp(help_attr(book->topics[i], "id"), id) == 0) return i;
  return -1;
}

static int help_entry(const help_book_t *book, const char *name) {
  int index = -1;
  for (int i = 0; i < zip_entry_count(book->zip); i++)
    if (strcmp(zip_entry_name(book->zip, i), name) == 0) {
      if (index >= 0) return -1;
      index = i;
    }
  return index;
}

static void help_book_free(help_book_t *book) {
  xmlFreeDoc(book->xml);
  zip_close(book->zip);
  free(book->bytes);
}

static bool help_read_archive(help_book_t *book, const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) return false;
  long size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
  if (size > 0 && size <= HELP_BOOK_LIMIT && fseek(file, 0, SEEK_SET) == 0)
    book->bytes = malloc((size_t)size);
  if (book->bytes && fread(book->bytes, 1, (size_t)size, file) == (size_t)size)
    book->zip = zip_open_memory(book->bytes, (size_t)size);
  fclose(file);
  return book->zip != NULL;
}

static int help_manifest(const help_book_t *book, const char *language) {
  char name[64];
  snprintf(name, sizeof(name), "book.%s.xml", language);
  int entry = help_entry(book, name);
  const char *region = strchr(language, '-');
  if (entry < 0 && region) {
    snprintf(name, sizeof(name), "book.%.*s.xml", (int)(region - language), language);
    entry = help_entry(book, name);
  }
  return entry >= 0 ? entry : help_entry(book, "book.xml");
}

static bool help_book_load(help_book_t *book, const char *path) {
  if (!help_read_archive(book, path)) return false;
  int entry = help_manifest(book, ui_get_language());
  if (entry < 0) return false;
  size_t size;
  uint8_t *bytes = zip_extract_limited(book->zip, entry, HELP_XML_LIMIT, &size);
  if (!bytes) return false;
  book->xml = xmlReadMemory((const char *)bytes, (int)size, path, NULL, XML_PARSE_NONET);
  free(bytes);
  if (!book->xml || book->xml->intSubset || book->xml->extSubset) return false;
  book->root = xmlDocGetRootElement(book->xml);
  if (!help_element(book->root, "help") || strcmp(help_attr(book->root, "version"), "1") != 0 ||
      !*help_attr(book->root, "title")) return false;
  for (xmlNodePtr topic = book->root->children; topic; topic = topic->next) {
    if (topic->type != XML_ELEMENT_NODE) continue;
    const char *id = help_attr(topic, "id");
    if (!help_element(topic, "topic") || !*id || !*help_attr(topic, "title") ||
        help_topic(book, id) >= 0 || book->count == HELP_TOPIC_LIMIT) return false;
    int blocks = 0;
    for (xmlNodePtr node = topic->children; node; node = node->next) {
      if (node->type != XML_ELEMENT_NODE) continue;
      if (++blocks > HELP_BLOCK_LIMIT) return false;
      if (help_element(node, "p") || help_element(node, "heading")) continue;
      if (!help_element(node, "image") ||
          !*help_attr(node, "caption") || help_entry(book, help_attr(node, "src")) < 0) return false;
    }
    if (!blocks) return false;
    book->topics[book->count++] = topic;
  }
  return book->count > 0;
}

// Fold ASCII, Latin-1 and Russian case independently of the process locale.
static uint32_t help_fold(uint32_t ch) {
  if ((ch >= 'A' && ch <= 'Z') || (ch >= 0x00c0 && ch <= 0x00de && ch != 0x00d7) ||
      (ch >= 0x0410 && ch <= 0x042f)) return ch + 0x20;
  if (ch == 0x0401) return 0x0451;  // Ё
  if (ch == 0x1e9e) return 0x00df;  // ẞ
  return ch;
}

static bool help_contains(const char *text, const char *query) {
  while (*text) {
    const char *value = text, *needle = query;
    font_sdf_utf8_next(&text);
    while (*needle && *value) {
      uint32_t a = help_fold(font_sdf_utf8_next(&value));
      uint32_t b = help_fold(font_sdf_utf8_next(&needle));
      if (a != b) break;
      if (!*needle) return true;
    }
  }
  return !*query;
}

static bool help_matches(xmlNodePtr topic, const char *query) {
  if (!*query || help_contains(ui_tr(help_attr(topic, "title")), query) ||
      help_contains(help_attr(topic, "keywords"), query)) return true;
  for (xmlNodePtr node = topic->children; node; node = node->next) {
    if (node->type != XML_ELEMENT_NODE) continue;
    if (help_element(node, "image")) {
      if (help_contains(ui_tr(help_attr(node, "caption")), query)) return true;
    } else {
      xmlChar *text = xmlNodeGetContent(node);
      bool match = text && help_contains(ui_tr((const char *)text), query);
      xmlFree(text);
      if (match) return true;
    }
  }
  return false;
}

static int help_topic_row(const help_viewer_t *v, int topic) {
  int count = (int)send_message(v->topics, RVM_GETITEMCOUNT, 0, NULL);
  for (int i = 0; i < count; i++) {
    reportview_item_t item;
    if (send_message(v->topics, RVM_GETITEMDATA, i, &item) && (int)item.userdata == topic) return i;
  }
  return -1;
}

static void help_filter(help_viewer_t *v) {
  send_message(v->topics, RVM_SETREDRAW, 0, NULL);
  send_message(v->topics, RVM_CLEAR, 0, NULL);
  for (int i = 0; i < v->book.count; i++) {
    xmlNodePtr topic = v->book.topics[i];
    if (!help_matches(topic, v->search->title)) continue;
    reportview_item_t item = { .text = ui_tr(help_attr(topic, "title")), .icon = -1, .userdata = (uint32_t)i };
    send_message(v->topics, RVM_ADDITEM, 0, &item);
  }
  int row = v->length ? help_topic_row(v, v->history[v->position]) : -1;
  if (row >= 0) send_message(v->topics, RVM_SETSELECTION, row, NULL);
  send_message(v->topics, RVM_SETREDRAW, 1, NULL);
  show_window(v->no_results, send_message(v->topics, RVM_GETITEMCOUNT, 0, NULL) == 0);
}

static bool help_label(window_t *parent, const char *text, uint8_t font, uint8_t color) {
  window_t *label = create_window(ui_tr(text), WINDOW_NOTITLE | WINDOW_NOFILL,
                                  MAKERECT(0, 0, 1, 1), parent, "Label", 0, NULL);
  if (!label) return false;
  label_create_params_t style = {.font = font, .color_index = color, .color_set = color != 0};
  send_message(label, lbSetStyle, 0, &style);
  return true;
}

static bool help_image(help_viewer_t *v, xmlNodePtr node) {
  int index = help_entry(&v->book, help_attr(node, "src"));
  size_t size;
  uint8_t *bytes = zip_extract_limited(v->book.zip, index, HELP_IMAGE_LIMIT, &size);
  if (!bytes) return false;
  window_t *image = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL,
                                  MAKERECT(0, 0, 1, 1), v->article, "ImageView", 0, NULL);
  image_view_data_t data = {bytes, size};
  bool ok = image && send_message(image, imageSetData, 0, &data);
  free(bytes);
  if (!ok) {
    if (image) destroy_window(image);
    return false;
  }
  image->layout.h_align = LAYOUT_ALIGN_START;
  send_message(image, imageSetFitWidth, 1, NULL);
  return true;
}

static void help_display(help_viewer_t *v, int index) {
  if (help_topic_row(v, index) < 0) {
    set_window_title(v->search, "");
    help_filter(v);
  }
  xmlNodePtr topic = v->book.topics[index];
  clear_window_children(v->article);
  bool ok = help_label(v->article, help_attr(topic, "title"), LABEL_TITLE, 0);
  for (xmlNodePtr node = topic->children; node && ok; node = node->next) {
    if (node->type != XML_ELEMENT_NODE) continue;
    if (help_element(node, "image")) {
      if (!help_image(v, node)) {
        fprintf(stderr, "[help] window=%u book='%s' cannot show image '%s'\n",
                get_root_window(v->article)->id, v->path, help_attr(node, "src"));
        fflush(stderr);
        help_label(v->article, "Screenshot unavailable.", LABEL_CAPTION, brTextSecondary);
      }
      ok = help_label(v->article, help_attr(node, "caption"), LABEL_CAPTION, brTextSecondary);
    } else {
      xmlChar *text = xmlNodeGetContent(node);
      ok = text && help_label(v->article, (const char *)text, help_element(node, "heading") ? LABEL_HEADING : LABEL_TEXT, 0);
      xmlFree(text);
    }
  }
  if (!ok) {
    fprintf(stderr, "[help] window=%u book='%s' cannot render topic '%s'\n",
            get_root_window(v->article)->id, v->path, help_attr(topic, "id"));
    fflush(stderr);
  }
  send_message(v->article, evResize, 0, NULL);
  send_message(v->article, evVScroll, 0, NULL);
  int row = help_topic_row(v, index);
  if (row >= 0) send_message(v->topics, RVM_SETSELECTION, row, NULL);
  enable_window(v->back, v->position > 0);
  enable_window(v->forward, v->position + 1 < v->length);
}

static void help_visit(help_viewer_t *v, int index) {
  if (v->length && v->history[v->position] == index) return;
  if (v->position + 1 == HELP_HISTORY_LIMIT) {
    memmove(v->history, v->history + 1, sizeof(v->history) - sizeof(v->history[0]));
    v->position--;
  }
  v->history[++v->position] = index;
  v->length = v->position + 1;
  help_display(v, index);
}

static result_t help_viewer_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  help_viewer_t *v = win->userdata;
  switch (msg) {
    case evCreate: {
      v = allocate_window_data(win, sizeof(*v));
      if (!v) return false;
      *v = *(const help_viewer_t *)lparam;
      return true;
    }
    case evPaint: return false;
    case evClose: destroy_window(win); return true;
    case evDestroy:
      if (!v) return true;
      clear_window_children(win);
      form_library_free(v->form);
      help_book_free(&v->book);
      free(v);
      win->userdata = NULL;
      return true;
    case evCommand: {
      if (!v || !v->topics) return false;
      uint32_t code = HIWORD(wparam);
      if (lparam == v->search && code == ednChange) {
        help_filter(v);
        return true;
      }
      if (lparam == v->topics && (code == RVN_SELCHANGE || code == RVN_DBLCLK)) {
        int row = (int)send_message(v->topics, RVM_GETSELECTION, 0, NULL);
        reportview_item_t item;
        if (row >= 0 && send_message(v->topics, RVM_GETITEMDATA, row, &item))
          help_visit(v, (int)item.userdata);
        return true;
      }
      if (code != btnClicked) return false;
      if (lparam == v->back && v->position > 0) v->position--;
      else if (lparam == v->forward && v->position + 1 < v->length) v->position++;
      else return false;
      help_display(v, v->history[v->position]);
      return true;
    }
  }
  return false;
}

bool help_open(window_t *owner, const char *path, const char *topic_id) {
  if (!is_window(owner) || !path || !*path) {
    fprintf(stderr, "[help] invalid owner or book path '%s'\n", path ? path : "");
    fflush(stderr);
    return false;
  }
  owner = get_root_window(owner);
  for (window_t *win = g_ui_runtime.windows; win; win = win->next) {
    if (win->owner != owner || win->proc != help_viewer_proc) continue;
    help_viewer_t *v = win->userdata;
    if (!v || strcmp(v->path, path) != 0) continue;
    if (topic_id) {
      int index = help_topic(&v->book, topic_id);
      if (index < 0) break;
      help_visit(v, index);
    }
    show_window(win, true);
    set_focus(v->topics);
    return true;
  }

  help_viewer_t init = { .position = -1 };
  char form_path[4096];
  int n = snprintf(form_path, sizeof(form_path), "%s/../share/orion/help/viewer.orion", ui_get_exe_dir());
  const form_def_t *form = NULL;
  int first = -1;
  if (strlen(path) < sizeof(init.path) && n > 0 && (size_t)n < sizeof(form_path) &&
      help_book_load(&init.book, path)) {
    first = topic_id ? help_topic(&init.book, topic_id) : 0;
    init.form = form_library_load(form_path);
    form = form_library_form(init.form, "help_viewer");
  }
  if (form && first >= 0) {
    strcpy(init.path, path);
    irect16_t frame = center_window_rect(R(0, 0, form->width, form->height), owner);
    window_t *win = create_window_from_form(form, frame.x, frame.y, NULL, help_viewer_proc, owner->hinstance, &init);
    if (win && win->userdata) {
      help_viewer_t *v = win->userdata;
      v->back       = get_window_item(win, form_library_control_id(v->form, form, "back"));
      v->forward    = get_window_item(win, form_library_control_id(v->form, form, "forward"));
      v->search     = get_window_item(win, form_library_control_id(v->form, form, "search"));
      v->topics     = get_window_item(win, form_library_control_id(v->form, form, "topics"));
      v->no_results = get_window_item(win, form_library_control_id(v->form, form, "no_results"));
      v->article    = get_window_item(win, form_library_control_id(v->form, form, "article"));
      if (v->back && v->forward && v->search && v->topics && v->no_results && v->article &&
          window_set_owner(win, owner)) {
        set_window_title(win, ui_tr(help_attr(v->book.root, "title")));
        reportview_column_t title_column = { .title = "" };
        send_message(v->topics, RVM_ADDCOLUMN, 0, &title_column);
        send_message(v->topics, RVM_SETCOLUMNTITLESVISIBLE, 0, NULL);
        help_filter(v);
        help_visit(v, first);
        show_window(win, true);
        set_focus(v->topics);
        return true;
      }
      init.form = NULL;
      init.book = (help_book_t){0};
    }
    if (win) destroy_window(win);
  }
  form_library_free(init.form);
  help_book_free(&init.book);
  fprintf(stderr, "[help] window=%u cannot open book='%s' topic='%s'\n", owner->id, path, topic_id ? topic_id : "");
  fflush(stderr);
  message_box(owner, ui_tr("The help book could not be opened."), ui_tr("Help Topics"), MB_OK);
  return false;
}
