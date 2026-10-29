#ifndef __ORION_XML_INCLUDE_H__
#define __ORION_XML_INCLUDE_H__
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
// Shared by the runtime loader and orionc. Includes are trusted package
// resources, resolved relative to the declaring file, never network entities.
static bool orion_xml_includes(xmlDocPtr doc, const char *origin, int depth) {
  if (depth > 16) { fprintf(stderr, "[orion] include nesting exceeds 16: %s\n", origin); return false; }
  xmlNodePtr root = xmlDocGetRootElement(doc);
  for (xmlNodePtr node = root ? root->children : NULL, next; node; node = next) {
    next = node->next;
    if (node->type != XML_ELEMENT_NODE || xmlStrcasecmp(node->name, BAD_CAST "include")) continue;
    xmlChar *file = xmlGetProp(node, BAD_CAST "file");
    if (!file || !*file || strstr((char *)file, "://")) { xmlFree(file); return false; }
    const char *slash = strrchr(origin, '/');
    char path[2048];
    int length = snprintf(path, sizeof(path), "%.*s/%s", slash ? (int)(slash - origin) : 1, slash ? origin : ".", file);
    xmlFree(file);
    if (length < 0 || length >= (int)sizeof(path)) return false;
    xmlDocPtr included = xmlReadFile(path, NULL, XML_PARSE_NONET);
    if (!included) return false;
    bool ok = orion_xml_includes(included, path, depth + 1);
    xmlNodePtr source = xmlDocGetRootElement(included);
    if (!source || xmlStrcasecmp(source->name, BAD_CAST "orion")) ok = false;
    if (ok) for (xmlNodePtr child = source->children; child; child = child->next)
      if (child->type == XML_ELEMENT_NODE) xmlAddPrevSibling(node, xmlDocCopyNode(child, doc, 1));
    xmlFreeDoc(included);
    if (!ok) return false;
    xmlUnlinkNode(node); xmlFreeNode(node);
  }
  return true;
}
#endif
