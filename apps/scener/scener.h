#ifndef SCENER_H
#define SCENER_H

#include <orion/ui.h>
#include <orion/gem.h>
#include "simplegl.h"

#include "build/generated/apps/scener/scener.h"

#define SIDE_PANEL_WIDTH 230

enum {
  ID_CP_TAB_CREATE,
  ID_CP_TAB_MODIFY,
  ID_CP_TAB_HIERARCHY,
  ID_CP_TAB_MOTION,
  ID_CP_TAB_DISPLAY,
  ID_CP_TAB_UTILITIES,

  ID_MODIFY_TAPER,
  ID_MODIFY_TWIST,
  ID_MODIFY_BEND,
  ID_MODIFY_STRETCH,
  ID_MODIFY_SKEW,
  ID_MODIFY_EXTRUDE,
  ID_MODIFY_MIRROR,
  ID_MODIFY_NOISE,
  ID_MODIFY_SHELL,
  ID_MODIFY_ARRAY,
};

typedef struct scene_doc_s {
  Scene           scene;
  char            filename[512];
  bool            modified;
  window_t       *win;
  window_t       *viewport_win;
  struct scene_doc_s *next;
} scene_doc_t;

typedef struct {
  scene_doc_t    *active_doc;
  scene_doc_t    *docs;
  window_t       *chrome_win;
  window_t       *menubar_win;
  window_t       *main_toolbar_win;
  window_t       *command_panel_win;
  window_t       *property_browser_win;
  hinstance_t     hinstance;
  accel_table_t  *accel;
  accel_table_t  *navigation_accel;
  bool            viewport_navigating;
  int             debug_flags;
} app_state_t;

extern app_state_t *g_app;

void scener_sync_main_toolbar(void);
void scener_sync_tool_ui(void);
void scener_sync_viewport_camera(scene_doc_t *doc);
uint16_t scener_active_tool(void);
void handle_menu_command(uint16_t id);
bool scener_create_primitive(scene_doc_t *doc, uint16_t id, vec3 ground_pos);
accel_table_t *scener_active_accelerators(void);

result_t scener_toolbar_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t scener_menubar_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
window_t *create_main_toolbar_window(void);

window_t *create_command_panel_window(void);
window_t *create_property_browser_window(void);
void property_browser_refresh(void);
void scener_rig_panel_refresh(void);
result_t win_property_browser(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
window_t *create_viewport_window(window_t *parent, scene_doc_t *doc);

scene_doc_t *create_document(const char *path);
scene_doc_t *create_document_ex(const char *path, bool show_windows);
void close_document(scene_doc_t *doc);
bool scener_open_file_path(const char *path);
void doc_update_title(scene_doc_t *doc);

#endif
