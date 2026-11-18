#include "comic_story.h"
#include <libxml/parser.h>
#include <strings.h>

#define FOG_SHEET_NAMES(key, name) \
  [FOG_SHOT_##key##_1] = name "-1", name "-2", name "-3", name "-4", name "-5", name "-6",
static const char *const comic_shots[] = {
  [FOG_SHOT_BOAST] = "boast",     [FOG_SHOT_DOCK] = "dock",
  [FOG_SHOT_LESSON] = "lesson",   [FOG_SHOT_KNOTS] = "knots",
  [FOG_SHOT_HUT] = "hut",         [FOG_SHOT_POINT] = "point",
  [FOG_SHOT_LANTERN] = "lantern", [FOG_SHOT_HARBOR] = "harbor",
  [FOG_SHOT_ROWING] = "rowing",   [FOG_SHOT_SAIL] = "sail",
  FOG_STORY_SHEETS(FOG_SHEET_NAMES)
};
#undef FOG_SHEET_NAMES

const char *comic_shot_name(fog_shot_t shot) {
  return shot > FOG_SHOT_NONE && shot < ARRAY_LEN(comic_shots) ? comic_shots[shot] : NULL;
}

static bool comic_xml_node(xmlNodePtr node, const char *name) {
  return node && node->type == XML_ELEMENT_NODE && !xmlStrcmp(node->name, BAD_CAST name);
}

static bool comic_xml_attr(xmlNodePtr node, const char *name, const char *expected) {
  xmlChar *value = xmlGetProp(node, BAD_CAST name);
  bool match = value && !xmlStrcmp(value, BAD_CAST expected);
  xmlFree(value);
  return match;
}

static char *comic_xml_speech(xmlNodePtr parent) {
  const char *language = ui_get_language();
  size_t base = strcspn(language, "-_");
  xmlNodePtr best = NULL;
  int best_rank = 0;
  for (xmlNodePtr node = parent->children; node; node = node->next) {
    if (!comic_xml_node(node, "text")) continue;
    const char *tag = (const char *)xmlGetProp(node, BAD_CAST "lang");
    int rank = !tag ? 0 : !strcasecmp(tag, language) ? 3 :
               strlen(tag) == base && !strncasecmp(tag, language, base) ? 2 :
               !strcasecmp(tag, "en") ? 1 : 0;
    xmlFree((void *)tag);
    if (rank > best_rank) { best = node; best_rank = rank; }
  }
  if (!best) return NULL;
  xmlChar *text = xmlNodeGetContent(best);
  if (text && *text && xmlStrlen(text) <= 512) return (char *)text;
  xmlFree(text);
  return NULL;
}

static fog_shot_t comic_xml_shot(xmlNodePtr panel) {
  xmlChar *shot = xmlGetProp(panel, BAD_CAST "shot");
  int i = 1;
  while (i < ARRAY_LEN(comic_shots) && xmlStrcmp(shot, BAD_CAST comic_shots[i])) i++;
  xmlFree(shot);
  return i < ARRAY_LEN(comic_shots) ? i : FOG_SHOT_NONE;
}

static int comic_xml_percent(xmlNodePtr node, const char *name, int fallback) {
  xmlChar *value = xmlGetProp(node, BAD_CAST name);
  if (!value) return fallback;
  const char *text = (const char *)value;
  long percent = strtol(text, NULL, 10);
  bool valid = *text && strspn(text, "0123456789") == strlen(text) && percent <= 100;
  xmlFree(value);
  return valid ? (int)percent : fallback;
}

static comic_bubble_position_t comic_xml_bubble(xmlNodePtr node) {
  xmlChar *name = xmlGetProp(node, BAD_CAST "bubble-edge");
  comic_bubble_edge_t edge = COMIC_BUBBLE_DEFAULT;
  if (!xmlStrcmp(name, BAD_CAST "top")) edge = COMIC_BUBBLE_TOP;
  else if (!xmlStrcmp(name, BAD_CAST "bottom")) edge = COMIC_BUBBLE_BOTTOM;
  xmlFree(name);
  return (comic_bubble_position_t){
    .edge = edge,
    .x = comic_xml_percent(node, "bubble-x", -1),
    .inset = comic_xml_percent(node, "bubble-inset", 0),
  };
}

static int comic_story_layer(comic_story_t *story, fog_shot_t shot) {
  for (int i = 0; i < story->layer_count; i++)
    if (story->layers[i] == shot) return i;
  if (story->layer_count == COMIC_STORY_MAX_LAYERS) return -1;
  story->layers[story->layer_count] = shot;
  return story->layer_count++;
}

static bool comic_story_page(comic_story_t *story, xmlNodePtr page) {
  int index = story->count, panel = 0;
  if (index >= COMIC_STORY_MAX_PAGES) return false;
  for (xmlNodePtr node = page->children; node; node = node->next) {
    if (node->type != XML_ELEMENT_NODE) continue;
    if (comic_xml_node(node, "quiz")) continue;  // Older archives include these between pages.
    if (!comic_xml_node(node, "panel") || panel == 3) return false;
    fog_shot_t shot = comic_xml_shot(node);
    if (!shot) return false;
    int layer = -1;
    if (comic_xml_attr(node, "paint", "true") && (layer = comic_story_layer(story, shot)) < 0) return false;
    story->pages[index][panel++] = (comic_beat_t){shot, comic_xml_speech(node), comic_xml_bubble(node), layer};
  }
  if (!panel) return false;
  story->count++;
  return true;
}

static bool comic_story_header(comic_story_t *story, xmlNodePtr root) {
  if (!comic_xml_node(root, "comic") || !comic_xml_attr(root, "version", "2") ||
      !comic_xml_attr(root, "mode", "fog-watercolor")) return false;
  xmlChar *id = xmlGetProp(root, BAD_CAST "id");
  const char *letters = "abcdefghijklmnopqrstuvwxyz0123456789-";
  size_t length = id ? strlen((const char *)id) : 0;
  bool valid = length > 0 && length < sizeof(story->id) && strspn((const char *)id, letters) == length;
  if (valid) memcpy(story->id, id, length);
  xmlFree(id);
  return valid;
}

static bool comic_story_parse(comic_story_t *story, xmlNodePtr root, int fallback_pages) {
  if (!comic_story_header(story, root)) return false;
  for (xmlNodePtr node = root->children; node; node = node->next) {
    if (node->type != XML_ELEMENT_NODE) continue;
    if (!story->count && comic_xml_node(node, "cover")) {
      story->pages[0][0] = (comic_beat_t){.shot = FOG_SHOT_ARCHIVE, .layer = -1};
      story->count++;
    } else if (!comic_xml_node(node, "page") || !comic_story_page(story, node)) {
      return false;
    }
  }
  return story->count == fallback_pages;
}

comic_story_t *comic_story_load(const comic_archive_t *archive) {
  size_t size;
  uint8_t *xml = comic_archive_read_entry(archive, "orion/story.xml", 64 * 1024, &size);
  if (!xml) return NULL;
  xmlDocPtr doc = xmlReadMemory((const char *)xml, (int)size, "orion/story.xml", NULL,
                                XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
  free(xml);
  if (!doc) return NULL;
  comic_story_t *story = doc->intSubset || doc->extSubset ? NULL : calloc(1, sizeof(*story));
  if (story) {
    if (!comic_story_parse(story, xmlDocGetRootElement(doc), comic_archive_pages(archive))) {
      comic_story_free(story);
      story = NULL;
    }
  }
  xmlFreeDoc(doc);
  return story;
}

void comic_story_free(comic_story_t *story) {
  if (!story) return;
  for (int page = 0; page < ARRAY_LEN(story->pages); page++)
    for (int panel = 0; panel < ARRAY_LEN(story->pages[page]); panel++)
      xmlFree(story->pages[page][panel].speech);
  free(story);
}
