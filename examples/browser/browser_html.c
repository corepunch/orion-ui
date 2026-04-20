#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/HTMLparser.h>
#include <libxml/tree.h>

#include "browser.h"

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
} strbuf_t;

static bool sb_reserve(strbuf_t *sb, size_t need) {
  if (!sb) return false;
  if (need <= sb->cap) return true;
  size_t cap = sb->cap ? sb->cap : 1024;
  while (cap < need) cap *= 2;
  char *p = (char *)realloc(sb->buf, cap);
  if (!p) return false;
  sb->buf = p;
  sb->cap = cap;
  return true;
}

static bool sb_append_c(strbuf_t *sb, char c) {
  if (!sb_reserve(sb, sb->len + 2)) return false;
  sb->buf[sb->len++] = c;
  sb->buf[sb->len] = '\0';
  return true;
}

static int trailing_newlines(const strbuf_t *sb) {
  int n = 0;
  if (!sb || !sb->buf) return 0;
  for (size_t i = sb->len; i > 0; i--) {
    if (sb->buf[i - 1] == '\n') n++;
    else break;
  }
  return n;
}

static void ensure_newlines(strbuf_t *sb, int count) {
  if (!sb || count <= 0) return;
  int have = trailing_newlines(sb);
  while (have < count) {
    if (!sb_append_c(sb, '\n')) return;
    have++;
  }
}

static bool is_block_tag(const char *name) {
  if (!name) return false;
  return
    strcmp(name, "p") == 0 || strcmp(name, "div") == 0 ||
    strcmp(name, "section") == 0 || strcmp(name, "article") == 0 ||
    strcmp(name, "header") == 0 || strcmp(name, "footer") == 0 ||
    strcmp(name, "main") == 0 || strcmp(name, "aside") == 0 ||
    strcmp(name, "ul") == 0 || strcmp(name, "ol") == 0 ||
    strcmp(name, "li") == 0 || strcmp(name, "pre") == 0 ||
    strcmp(name, "blockquote") == 0 ||
    strcmp(name, "h1") == 0 || strcmp(name, "h2") == 0 ||
    strcmp(name, "h3") == 0 || strcmp(name, "h4") == 0 ||
    strcmp(name, "h5") == 0 || strcmp(name, "h6") == 0;
}

static bool is_ignored_tag(const char *name) {
  if (!name) return false;
  return strcmp(name, "script") == 0 || strcmp(name, "style") == 0 || strcmp(name, "noscript") == 0;
}

static void append_text_normalized(strbuf_t *out, const char *text) {
  if (!out || !text) return;
  bool prev_space = (out->len > 0) ? isspace((unsigned char)out->buf[out->len - 1]) : true;
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    if (isspace(*p)) {
      if (!prev_space) {
        if (!sb_append_c(out, ' ')) return;
        prev_space = true;
      }
    } else {
      if (!sb_append_c(out, (char)*p)) return;
      prev_space = false;
    }
  }
}

static void trim_in_place(char *s) {
  if (!s) return;
  size_t start = 0;
  size_t len = strlen(s);
  while (start < len && isspace((unsigned char)s[start])) start++;
  size_t end = len;
  while (end > start && isspace((unsigned char)s[end - 1])) end--;
  if (start > 0) memmove(s, s + start, end - start);
  s[end - start] = '\0';
}

static void collapse_spaces_in_place(char *s) {
  if (!s) return;
  size_t r = 0;
  size_t w = 0;
  bool prev_space = false;
  while (s[r]) {
    unsigned char ch = (unsigned char)s[r++];
    if (isspace(ch)) {
      if (!prev_space) {
        s[w++] = ' ';
        prev_space = true;
      }
    } else {
      s[w++] = (char)ch;
      prev_space = false;
    }
  }
  s[w] = '\0';
}

static void html_to_text_walk(xmlNode *node, strbuf_t *out) {
  for (xmlNode *n = node; n; n = n->next) {
    if (n->type == XML_TEXT_NODE) {
      append_text_normalized(out, (const char *)n->content);
      continue;
    }

    if (n->type != XML_ELEMENT_NODE) {
      if (n->children) html_to_text_walk(n->children, out);
      continue;
    }

    const char *name = (const char *)n->name;
    if (is_ignored_tag(name)) continue;

    if (strcmp(name, "br") == 0) {
      ensure_newlines(out, 1);
      continue;
    }

    bool block = is_block_tag(name);
    if (block && out->len > 0) ensure_newlines(out, 2);
    if (n->children) html_to_text_walk(n->children, out);
    if (block && out->len > 0) ensure_newlines(out, 2);
  }
}

char *browser_html_to_plain_text(const char *html, size_t len) {
  if (!html) return strdup("(empty response)");

  htmlDocPtr doc = htmlReadMemory(
    html,
    (int)len,
    NULL,
    NULL,
    HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING
  );
  if (!doc) return strdup("(failed to parse HTML)");

  strbuf_t out = {0};
  xmlNode *root = xmlDocGetRootElement(doc);
  if (root) html_to_text_walk(root, &out);
  xmlFreeDoc(doc);

  if (!out.buf || out.len == 0) {
    free(out.buf);
    return strdup("(no visible text content)");
  }

  while (out.len > 0 && isspace((unsigned char)out.buf[out.len - 1])) out.len--;
  out.buf[out.len] = '\0';
  return out.buf;
}

char *browser_html_extract_title(const char *html, size_t len) {
  if (!html) return NULL;

  htmlDocPtr doc = htmlReadMemory(
    html,
    (int)len,
    NULL,
    NULL,
    HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING
  );
  if (!doc) return NULL;

  char *title = NULL;
  xmlNode *root = xmlDocGetRootElement(doc);
  if (root) {
    for (xmlNode *n = root->children; n; n = n->next) {
      if (n->type != XML_ELEMENT_NODE || strcmp((const char *)n->name, "head") != 0)
        continue;

      for (xmlNode *h = n->children; h; h = h->next) {
        if (h->type == XML_ELEMENT_NODE && strcmp((const char *)h->name, "title") == 0) {
          xmlChar *txt = xmlNodeGetContent(h);
          if (txt) {
            title = strdup((const char *)txt);
            xmlFree(txt);
          }
          break;
        }
      }
      break;
    }
  }

  xmlFreeDoc(doc);

  if (!title) return NULL;
  collapse_spaces_in_place(title);
  trim_in_place(title);
  if (!title[0]) {
    free(title);
    return NULL;
  }
  return title;
}
