#include "user.h"

#define FE_MAX_COMPONENT_PLUGINS 32

typedef struct {
  fe_component_desc_t desc;
} fe_component_entry_t;

#define FE_ICON_STRIP_COLS 16
#define FE_DEFAULT_TOOLBOX_ICON 0

static fe_component_entry_t g_components[FE_MAX_COMPONENTS];
static int g_component_count = 0;
static fe_icon_strip_t g_toolbox_icon_strip = {0};
static uint8_t *g_toolbox_icon_pixels = NULL;
static int g_toolbox_icon_count = 0;
static int g_default_toolbox_icon = FE_DEFAULT_TOOLBOX_ICON;

static void *g_component_plugin_handles[FE_MAX_COMPONENT_PLUGINS];
static int g_component_plugin_count = 0;

static bool fe_component_exists(const char *class_name, const char *token) {
  for (int i = 0; i < g_component_count; i++) {
    const fe_component_desc_t *d = &g_components[i].desc;
    if (class_name && d->class_name && strcmp(class_name, d->class_name) == 0)
      return true;
    if (token && d->token && strcmp(token, d->token) == 0)
      return true;
  }
  return false;
}

static bool fe_icon_strip_valid(const fe_icon_strip_t *strip) {
  if (!strip || !strip->rgba || strip->width <= 0 || strip->height <= 0 ||
      strip->icon_size <= 0)
    return false;
  if ((strip->width % strip->icon_size) != 0 ||
      (strip->height % strip->icon_size) != 0)
    return false;
  return true;
}

static int fe_icon_strip_count(const fe_icon_strip_t *strip) {
  if (!fe_icon_strip_valid(strip)) return 0;
  return (strip->width / strip->icon_size) * (strip->height / strip->icon_size);
}

static bool fe_append_toolbox_icon_strip(const fe_icon_strip_t *strip, int *out_base) {
  if (out_base) *out_base = -1;
  if (!fe_icon_strip_valid(strip)) return false;

  int incoming_count = fe_icon_strip_count(strip);
  if (incoming_count <= 0) return false;

  int tile = strip->icon_size;
  if (g_toolbox_icon_strip.rgba && g_toolbox_icon_strip.icon_size != tile)
    return false;

  int old_count = g_toolbox_icon_count;
  int new_count = old_count + incoming_count;
  int cols = FE_ICON_STRIP_COLS;
  int rows = (new_count + cols - 1) / cols;
  int new_w = cols * tile;
  int new_h = rows * tile;
  uint8_t *new_pixels = calloc((size_t)new_w * new_h * 4, 1);
  if (!new_pixels) return false;

  if (g_toolbox_icon_pixels) {
    int old_w = g_toolbox_icon_strip.width;
    int old_h = g_toolbox_icon_strip.height;
    for (int y = 0; y < old_h; y++) {
      memcpy(new_pixels + (size_t)y * new_w * 4,
             g_toolbox_icon_pixels + (size_t)y * old_w * 4,
             (size_t)old_w * 4);
    }
  }

  int src_cols = strip->width / tile;
  for (int i = 0; i < incoming_count; i++) {
    int src_x = (i % src_cols) * tile;
    int src_y = (i / src_cols) * tile;
    int dst_idx = old_count + i;
    int dst_x = (dst_idx % cols) * tile;
    int dst_y = (dst_idx / cols) * tile;
    for (int y = 0; y < tile; y++) {
      memcpy(new_pixels + ((size_t)(dst_y + y) * new_w + dst_x) * 4,
             strip->rgba + ((size_t)(src_y + y) * strip->width + src_x) * 4,
             (size_t)tile * 4);
    }
  }

  free(g_toolbox_icon_pixels);
  g_toolbox_icon_pixels = new_pixels;
  g_toolbox_icon_count = new_count;
  g_toolbox_icon_strip = (fe_icon_strip_t){
    .rgba = g_toolbox_icon_pixels,
    .width = new_w,
    .height = new_h,
    .icon_size = tile,
    .default_icon = g_default_toolbox_icon,
  };

  if (strip->default_icon >= 0 && strip->default_icon < incoming_count)
    g_default_toolbox_icon = old_count + strip->default_icon;
  g_toolbox_icon_strip.default_icon = g_default_toolbox_icon;

  if (out_base) *out_base = old_count;
  return true;
}

static bool fe_register_component_ex(const fe_component_desc_t *desc,
                                     int icon_base,
                                     int plugin_icon_count) {
  if (!desc || !desc->class_name || !desc->display_name || !desc->token ||
      !desc->name_prefix || !desc->proc)
    return false;
  if (g_component_count >= FE_MAX_COMPONENTS)
    return false;
  if (fe_component_exists(desc->class_name, desc->token))
    return false;
  if (!register_window_class(desc))
    return false;
  fe_component_desc_t stored = *desc;
  if (icon_base >= 0 && desc->toolbox_icon >= 0 &&
      desc->toolbox_icon < plugin_icon_count) {
    stored.toolbox_icon = icon_base + desc->toolbox_icon;
  } else if (g_toolbox_icon_strip.rgba) {
    stored.toolbox_icon = g_default_toolbox_icon;
  }
  g_components[g_component_count].desc = stored;
  g_component_count++;
  return true;
}

bool fe_register_component(const fe_component_desc_t *desc) {
  return fe_register_component_ex(desc, -1, 0);
}

int fe_component_count(void) {
  return g_component_count;
}

const fe_component_desc_t *fe_component_at(int index) {
  if (index < 0 || index >= g_component_count)
    return NULL;
  return &g_components[index].desc;
}

const fe_component_desc_t *fe_component_by_id(int id) {
  return fe_component_at(id);
}

const fe_component_desc_t *fe_component_by_tool_ident(int ident) {
  for (int i = 0; i < g_component_count; i++) {
    const fe_component_desc_t *d = &g_components[i].desc;
    if (d->toolbox_ident == ident)
      return d;
  }
  return NULL;
}

const fe_component_desc_t *fe_component_by_token(const char *token) {
  if (!token || !*token)
    return NULL;
  for (int i = 0; i < g_component_count; i++) {
    const fe_component_desc_t *d = &g_components[i].desc;
    if (strcmp(d->token, token) == 0)
      return d;
  }
  return NULL;
}

bool fe_component_toolbox_icon_strip(fe_icon_strip_t *out) {
  if (!out || !g_toolbox_icon_strip.rgba ||
      g_toolbox_icon_strip.width <= 0 ||
      g_toolbox_icon_strip.height <= 0 ||
      g_toolbox_icon_strip.icon_size <= 0)
    return false;
  *out = g_toolbox_icon_strip;
  return true;
}

bool fe_load_component_plugin(const char *path) {
  if (!path || !*path)
    return false;
  if (g_component_plugin_count >= FE_MAX_COMPONENT_PLUGINS)
    return false;

  void *handle = axDynlibOpen(path);
  if (!handle)
    return false;

  fe_plugin_class_count_fn count_fn =
      (fe_plugin_class_count_fn)axDynlibSym(handle, "fe_plugin_class_count");
  fe_plugin_class_desc_fn desc_fn =
      (fe_plugin_class_desc_fn)axDynlibSym(handle, "fe_plugin_class_desc");
  fe_plugin_toolbox_icon_strip_fn icon_strip_fn =
      (fe_plugin_toolbox_icon_strip_fn)axDynlibSym(handle, "fe_plugin_toolbox_icon_strip");

  if (!count_fn || !desc_fn) {
    axDynlibClose(handle);
    return false;
  }

  int n = count_fn();
  bool has_new_components = false;
  for (int i = 0; i < n; i++) {
    const fe_component_desc_t *d = desc_fn(i);
    if (d && !fe_component_exists(d->class_name, d->token)) {
      has_new_components = true;
      break;
    }
  }

  int icon_base = -1;
  int plugin_icon_count = 0;
  if (has_new_components && icon_strip_fn) {
    fe_icon_strip_t strip = {0};
    if (icon_strip_fn(&strip)) {
      plugin_icon_count = fe_icon_strip_count(&strip);
      if (!fe_append_toolbox_icon_strip(&strip, &icon_base))
        plugin_icon_count = 0;
    }
  }

  for (int i = 0; i < n; i++) {
    const fe_component_desc_t *d = desc_fn(i);
    if (d) fe_register_component_ex(d, icon_base, plugin_icon_count);
  }

  g_component_plugin_handles[g_component_plugin_count++] = handle;
  return true;
}

void fe_unload_component_plugins(void) {
  for (int i = g_component_plugin_count - 1; i >= 0; i--)
    axDynlibClose(g_component_plugin_handles[i]);
  g_component_plugin_count = 0;
  free(g_toolbox_icon_pixels);
  g_toolbox_icon_pixels = NULL;
  g_toolbox_icon_count = 0;
  g_toolbox_icon_strip = (fe_icon_strip_t){0};
  g_default_toolbox_icon = FE_DEFAULT_TOOLBOX_ICON;
}
