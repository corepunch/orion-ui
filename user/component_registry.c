#include "user.h"
#include "icons.h"
#include "../examples/formeditor/controls-icons.h"

static bool is_valid_toolbox_icon(int icon) {
  if (icon >= SYSICON_BASE && icon <= sysicon_yield_add)
    return true;
  if (icon >= 0 && icon < IC_ICON_COUNT)
    return true;
  return false;
}

#define FE_MAX_COMPONENT_PLUGINS 32

typedef struct {
  fe_component_desc_t desc;
} fe_component_entry_t;

static fe_component_entry_t g_components[FE_MAX_COMPONENTS];
static int g_component_count = 0;

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

bool fe_register_component(const fe_component_desc_t *desc) {
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
  if (!is_valid_toolbox_icon(stored.toolbox_icon))
    stored.toolbox_icon = sysicon_puzzle;

  g_components[g_component_count].desc = stored;
  g_component_count++;
  return true;
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

  if (!count_fn || !desc_fn) {
    axDynlibClose(handle);
    return false;
  }

  int n = count_fn();
  for (int i = 0; i < n; i++) {
    const fe_component_desc_t *d = desc_fn(i);
    if (d) fe_register_component(d);
  }

  g_component_plugin_handles[g_component_plugin_count++] = handle;
  return true;
}

void fe_unload_component_plugins(void) {
  for (int i = g_component_plugin_count - 1; i >= 0; i--)
    axDynlibClose(g_component_plugin_handles[i]);
  g_component_plugin_count = 0;
}
