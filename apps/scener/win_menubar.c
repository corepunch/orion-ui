#include "scener.h"
#include <orion/user/draw.h>

static scene_doc_t *current_doc(void);

result_t scener_toolbar_proc(window_t *win, uint32_t msg,
                             uint32_t wparam, void *lparam) {
  (void)lparam;
  switch (msg) {
    case evCreate:
      scener_sync_command_state();
      return true;
    case evCommand:
      if (HIWORD(wparam) != btnClicked) return false;
      handle_menu_command(LOWORD(wparam));
      scener_sync_command_state();
      return true;
    case evDestroy:
      if (g_app && g_app->main_toolbar_win == win) g_app->main_toolbar_win = NULL;
      return false;
    default:
      return false;
  }
}

window_t *create_main_toolbar_window(void) {
  if (!g_app) return NULL;
  if (g_app->chrome_win) return app_chrome_toolbar(g_app->chrome_win);
  g_app->chrome_win = create_application_chrome("SimpleSketch3D Chrome", NULL, NULL, 0,
      scener_toolbar_proc, &scener_application_toolbar, g_app->hinstance);
  window_t *win = app_chrome_toolbar(g_app->chrome_win);
  if (!win) return NULL;
  show_window(win, true);
  g_app->main_toolbar_win = win;
  scener_sync_command_state();
  return win;
}

void scener_sync_command_state(void) {
  if (!g_app) return;
  scener_sync_panel_checks();
  uint16_t active = scener_active_tool();
  if (g_app->main_toolbar_win)
    send_message(g_app->main_toolbar_win, tbCheckButton, active, (void *)(intptr_t)1);
#ifndef BUILD_AS_GEM
  window_t *ribbon = app_chrome_ribbon(g_app->chrome_win);
  if (!ribbon) return;
  scene_doc_t *doc = current_doc();
  uint16_t create = doc ? doc->scene.createMode : 0;
  for (uint16_t id = ID_TOOL_SELECT; id <= ID_TOOL_SCALE; id++) {
    uintptr_t state = !doc ? TBSTATE_DISABLED : !create && id == active ? TBSTATE_CHECKED : 0;
    send_message(ribbon, rbSetState, id, (void *)state);
  }
  for (uint16_t id = ID_CREATE_BOX; id <= ID_CREATE_DOOR_GOTHIC; id++) {
    uintptr_t state = !doc ? TBSTATE_DISABLED : id == create ? TBSTATE_CHECKED : 0;
    send_message(ribbon, rbSetState, id, (void *)state);
  }
  const uint16_t document_commands[] = {
    ID_CREATE_POINT_LIGHT, ID_CREATE_DIRECTIONAL_LIGHT, ID_CREATE_CAMERA,
    ID_ANIMATION_PLAY, ID_ANIMATION_REWIND,
  };
  for (size_t i = 0; i < ARRAY_LEN(document_commands); i++)
    send_message(ribbon, rbSetState, document_commands[i], (void *)(uintptr_t)(doc ? 0 : TBSTATE_DISABLED));
  uintptr_t cameras = doc && doc->scene.ncameras > 1 ? 0 : TBSTATE_DISABLED;
  send_message(ribbon, rbSetState, ID_VIEW_PREV_CAMERA, (void *)cameras);
  send_message(ribbon, rbSetState, ID_VIEW_NEXT_CAMERA, (void *)cameras);
  send_message(ribbon, rbSetState, ID_VIEW_SHOW_SHADOWS,
               (void *)(uintptr_t)((g_app->debug_flags & DBG_NO_SHADOWS) ? 0 : TBSTATE_CHECKED));
  send_message(ribbon, rbSetState, ID_VIEW_TOGGLE_SSAO,
               (void *)(uintptr_t)((g_app->debug_flags & DBG_SSAO) ? TBSTATE_CHECKED : 0));
#endif
}

void scener_sync_tool_ui(void) {
  if (!g_app) return;
  scener_sync_command_state();
  property_browser_refresh();
  if (g_app->command_panel_win) invalidate_window(g_app->command_panel_win);
  for (scene_doc_t *doc = g_app->docs; doc; doc = doc->next)
    if (doc->viewport_win) invalidate_window(doc->viewport_win);
}

static bool dispatch_command(uint32_t msg, uint32_t wparam) {
  if (msg == evCommand) {
    uint16_t notif = HIWORD(wparam);
    if (notif == kMenuBarNotificationItemClick ||
        notif == kAcceleratorNotification      ||
        notif == btnClicked) {
      handle_menu_command(LOWORD(wparam));
      return true;
    }
  }
  return false;
}

result_t scener_menubar_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  return dispatch_command(msg, wparam) || win_menubar(win, msg, wparam, lparam);
}

result_t scener_ribbon_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (dispatch_command(msg, wparam)) return true;
  return win_ribbon(win, msg, wparam, lparam);
}

static scene_doc_t *current_doc(void) {
  if (!g_app) return NULL;
  scene_doc_t *doc = g_app->active_doc;
  if (!doc) doc = g_app->docs;
  return doc;
}

static bool is_create_primitive(uint16_t id) {
  return id >= ID_CREATE_BOX && id <= ID_CREATE_DOOR_GOTHIC;
}

enum {
  CREATE_SIZE_CM = 100,
  CREATE_RADIUS_CM = 50,
  CREATE_SPHERE_STEPS = 16,
  CREATE_ROUND_SIDES = 24,
  CREATE_PRISM_SIDES = 6,
  CREATE_TORUS_RADIUS_CM = 40,
  CREATE_TORUS_TUBE_CM = 15,
  CREATE_CAPSULE_RADIUS_CM = 30,
  CREATE_CAPSULE_CYLINDER_CM = 60,
  CREATE_ARCH_DEPTH_CM = 30,
  CREATE_ARCH_FRAME_CM = 15,
  CREATE_DETAIL_STEPS = 12,
};

typedef struct {
  const char *label;
  double number;
  int decimals;
} creation_property_t;

static const creation_property_t kCreationProperties[][6] = {
  [ID_CREATE_BOX - ID_CREATE_BOX] = {
    { "Width (cm)", CREATE_SIZE_CM, 0 }, { "Height (cm)", CREATE_SIZE_CM, 0 }, { "Depth (cm)", CREATE_SIZE_CM, 0 },
  },
  [ID_CREATE_RECT - ID_CREATE_BOX] = {
    { "Width (cm)", 100, 0 }, { "Height (cm)", 100, 0 }, { "Extrude (cm)", 20, 0 }, { "Bevel (cm)", 2, 0 },
  },
  [ID_CREATE_CIRCLE - ID_CREATE_BOX] = {
    { "Radius (cm)", 50, 0 }, { "Segments", 32, 0 }, { "Extrude (cm)", 20, 0 }, { "Bevel (cm)", 2, 0 },
  },
  [ID_CREATE_ELLIPSE - ID_CREATE_BOX] = {
    { "Width (cm)", 100, 0 }, { "Height (cm)", 70, 0 }, { "Segments", 48, 0 }, { "Extrude (cm)", 20, 0 }, { "Bevel (cm)", 2, 0 },
  },
  [ID_CREATE_STAR - ID_CREATE_BOX] = {
    { "Outer Radius (cm)", 50, 0 }, { "Inner Radius (cm)", 25, 0 }, { "Points", 5, 0 }, { "Extrude (cm)", 20, 0 }, { "Bevel (cm)", 2, 0 },
  },
  [ID_CREATE_SCREEN - ID_CREATE_BOX] = {
    { "Width (cm)", 7.03, 2 }, { "Height (cm)", 15.54, 2 }, { "Depth (cm)", 0.03, 2 }, { "Corner Radius (cm)", 0.98, 2 },
  },
  [ID_CREATE_SPHERE - ID_CREATE_BOX] = {
    { "Radius (cm)", CREATE_RADIUS_CM, 0 }, { "Rings", CREATE_SPHERE_STEPS, 0 }, { "Slices", CREATE_SPHERE_STEPS, 0 },
  },
  [ID_CREATE_CYLINDER - ID_CREATE_BOX] = {
    { "Radius (cm)", CREATE_RADIUS_CM, 0 }, { "Height (cm)", CREATE_SIZE_CM, 0 }, { "Sides", CREATE_ROUND_SIDES, 0 },
  },
  [ID_CREATE_CONE - ID_CREATE_BOX] = {
    { "Base Radius (cm)", CREATE_RADIUS_CM, 0 }, { "Top Radius (cm)", 0, 0 }, { "Height (cm)", CREATE_SIZE_CM, 0 }, { "Sides", CREATE_ROUND_SIDES, 0 },
  },
  [ID_CREATE_TORUS - ID_CREATE_BOX] = {
    { "Major Radius (cm)", CREATE_TORUS_RADIUS_CM, 0 }, { "Minor Radius (cm)", CREATE_TORUS_TUBE_CM, 0 },
    { "Major Segments", CREATE_ROUND_SIDES, 0 }, { "Minor Segments", CREATE_DETAIL_STEPS, 0 },
  },
  [ID_CREATE_PRISM - ID_CREATE_BOX] = {
    { "Radius (cm)", CREATE_RADIUS_CM, 0 }, { "Height (cm)", CREATE_SIZE_CM, 0 }, { "Sides", CREATE_PRISM_SIDES, 0 },
  },
  [ID_CREATE_CAPSULE - ID_CREATE_BOX] = {
    { "Radius (cm)", CREATE_CAPSULE_RADIUS_CM, 0 }, { "Cylinder Height (cm)", CREATE_CAPSULE_CYLINDER_CM, 0 },
    { "Rings", CREATE_DETAIL_STEPS, 0 }, { "Slices", CREATE_DETAIL_STEPS, 0 },
  },
  [ID_CREATE_ARCH - ID_CREATE_BOX] = {
    { "Width (cm)", CREATE_SIZE_CM, 0 }, { "Height (cm)", CREATE_SIZE_CM, 0 }, { "Depth (cm)", CREATE_ARCH_DEPTH_CM, 0 },
    { "Frame (cm)", CREATE_ARCH_FRAME_CM, 0 }, { "Segments", CREATE_DETAIL_STEPS, 0 },
  },
  [ID_CREATE_WINDOW_ROUND - ID_CREATE_BOX] = { { "Width (cm)", 120, 0 }, { "Height (cm)", 180, 0 } },
  [ID_CREATE_WINDOW_COTTAGE - ID_CREATE_BOX] = { { "Width (cm)", 120, 0 }, { "Height (cm)", 140, 0 } },
  [ID_CREATE_WINDOW_GOTHIC - ID_CREATE_BOX] = { { "Width (cm)", 120, 0 }, { "Height (cm)", 220, 0 } },
  [ID_CREATE_DOOR_RECTANGULAR - ID_CREATE_BOX] = { { "Width (cm)", 100, 0 }, { "Height (cm)", 210, 0 } },
  [ID_CREATE_DOOR_ROUND - ID_CREATE_BOX] = { { "Width (cm)", 100, 0 }, { "Height (cm)", 210, 0 } },
  [ID_CREATE_DOOR_GOTHIC - ID_CREATE_BOX] = { { "Width (cm)", 100, 0 }, { "Height (cm)", 210, 0 } },
};

void scener_visit_creation_properties(uint16_t command, void (*visit)(void *, const char *, const char *), void *context) {
  if (!visit || !is_create_primitive(command)) return;
  for (size_t i = 0; i < ARRAY_LEN(scener_action_meta); i++) {
    if (scener_action_meta[i].id == command) {
      visit(context, ui_tr("Object"), ui_tr(scener_action_meta[i].label));
      break;
    }
  }
  const creation_property_t *props = kCreationProperties[command - ID_CREATE_BOX];
  for (size_t i = 0; i < ARRAY_LEN(kCreationProperties[0]) && props[i].label; i++) {
    char value[64];
    visit(context, ui_tr(props[i].label), ui_format_number(props[i].number, props[i].decimals, value, sizeof(value)));
  }
}

bool scener_create_primitive(scene_doc_t *doc, uint16_t id, vec3 ground_pos) {
  if (!doc || !is_create_primitive(id)) return false;
  const char *profile_tag = NULL;
  switch (id) {
    case ID_CREATE_RECT:         profile_tag = "rect"; break;
    case ID_CREATE_CIRCLE:       profile_tag = "circle"; break;
    case ID_CREATE_ELLIPSE:      profile_tag = "ellipse"; break;
    case ID_CREATE_STAR:         profile_tag = "star"; break;
    case ID_CREATE_SCREEN:       profile_tag = "screen"; break;
  }
  if (profile_tag) {
    if (!scene_create_promo_shape(&doc->scene, profile_tag, ground_pos)) return false;
    doc->modified = true;
    doc_update_title(doc);
    property_browser_refresh();
    if (doc->viewport_win) invalidate_window(doc->viewport_win);
    return true;
  }
  if (id >= ID_CREATE_WINDOW_ROUND && id <= ID_CREATE_DOOR_GOTHIC) {
    bool door = id >= ID_CREATE_DOOR_RECTANGULAR;
    const char *preset = door
      ? (id == ID_CREATE_DOOR_RECTANGULAR ? "rectangular" : id == ID_CREATE_DOOR_ROUND ? "round-arch" : "gothic")
      : (id == ID_CREATE_WINDOW_ROUND ? "round-arch" : id == ID_CREATE_WINDOW_COTTAGE ? "cottage" : "gothic");
    if (!(door ? scene_create_door(&doc->scene, preset, ground_pos) : scene_create_window(&doc->scene, preset, ground_pos))) return false;
    doc->modified = true;
    doc_update_title(doc);
    property_browser_refresh();
    if (doc->viewport_win) invalidate_window(doc->viewport_win);
    return true;
  }
  Mesh m = {0};
  const float size = CREATE_SIZE_CM / 100.0f, radius = CREATE_RADIUS_CM / 100.0f;
  float lift = size / 2.0f;
  switch (id) {
    case ID_CREATE_BOX:      m = gen_box(size, size, size); break;
    case ID_CREATE_SPHERE:   m = gen_sphere(radius, CREATE_SPHERE_STEPS, CREATE_SPHERE_STEPS); lift = radius; break;
    case ID_CREATE_CYLINDER: m = gen_cylinder(radius, size, CREATE_ROUND_SIDES); break;
    case ID_CREATE_CONE:     m = gen_cone(radius, 0, size, CREATE_ROUND_SIDES, 1); break;
    case ID_CREATE_TORUS:    lift = CREATE_TORUS_TUBE_CM / 100.0f;
                            m = gen_torus(CREATE_TORUS_RADIUS_CM / 100.0f, lift, CREATE_ROUND_SIDES, CREATE_DETAIL_STEPS); break;
    case ID_CREATE_PRISM:    m = gen_prism(radius, size, CREATE_PRISM_SIDES); break;
    case ID_CREATE_CAPSULE:  lift = CREATE_CAPSULE_RADIUS_CM / 100.0f;
                            m = gen_capsule(lift, CREATE_CAPSULE_CYLINDER_CM / 100.0f, CREATE_DETAIL_STEPS, CREATE_DETAIL_STEPS); break;
    case ID_CREATE_ARCH:     m = gen_arch(size, size, CREATE_ARCH_DEPTH_CM / 100.0f, CREATE_ARCH_FRAME_CM / 100.0f, CREATE_DETAIL_STEPS, 0); break;
    default: return false;
  }
  mat4 M = mat4_translate(vadd(ground_pos, v3(0, lift, 0)));
  doc->scene.activeEditNode = NULL;
  doc->scene.activeEditMatrix = M;
  scene_add_obj(&doc->scene, m, M, mat4_identity(), v3(0.7f, 0.7f, 0.7f), 32, 1, 1, 0);
  doc->scene.activeEditMatrix = mat4_identity();
  doc->scene.selectedObj = doc->scene.nobjs - 1;
  doc->scene.selectedNode = NULL;
  scene_build_all_shadow_volumes(&doc->scene);
  doc->modified = true;
  doc_update_title(doc);
  property_browser_refresh();
  if (doc->viewport_win) invalidate_window(doc->viewport_win);
  return true;
}

uint16_t scener_active_tool(void) {
  scene_doc_t *doc = current_doc();
  if (!doc) return ID_TOOL_SELECT;
  return doc->scene.editMode == EDIT_W_MOVE   ? ID_TOOL_MOVE
       : doc->scene.editMode == EDIT_E_ROTATE ? ID_TOOL_ROTATE
       : doc->scene.editMode == EDIT_R_SCALE  ? ID_TOOL_SCALE
       :                                        ID_TOOL_SELECT;
}

void handle_menu_command(uint16_t id) {
  if (!g_app) return;
  scene_doc_t *doc = current_doc();

  switch (id) {
    case ID_FILE_NEW:
      create_document(NULL);
      break;

    case ID_FILE_OPEN: {
      char path[512] = {0};
      openfilename_t ofn = {0};
      ofn.lStructSize = sizeof(ofn);
      ofn.hwndOwner = g_app->menubar_win;
      ofn.lpstrFile = path;
      ofn.nMaxFile = sizeof(path);
      ofn.lpstrFilter = "Scene files\0*.blks\0All files\0*.*\0";
      ofn.Flags = OFN_FILEMUSTEXIST;
      if (get_open_filename(&ofn))
        scener_open_file_path(path);
      break;
    }

    case ID_FILE_SAVE:
      if (doc) {
        if (!doc->filename[0]) goto do_save_as;
        scene_save_all(&doc->scene);
        doc->modified = false;
      }
      break;

    do_save_as:
    case ID_FILE_SAVEAS:
      if (doc) {
        char path[512] = {0};
        openfilename_t ofn = {0};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = g_app->menubar_win;
        ofn.lpstrFile = path;
        ofn.nMaxFile = sizeof(path);
        ofn.lpstrFilter = "Scene files\0*.blks\0All files\0*.*\0";
        ofn.Flags = OFN_OVERWRITEPROMPT;
        if (get_save_filename(&ofn)) {
          strncpy(doc->filename, path, sizeof(doc->filename)-1);
          doc->filename[sizeof(doc->filename)-1] = '\0';
          snprintf(doc->scene.scenePath, sizeof(doc->scene.scenePath), "%s", path);
          if (scene_save_all(&doc->scene)) doc->modified = false;
          doc_update_title(doc);
        }
      }
      break;

    case ID_FILE_CLOSE:
      if (doc) close_document(doc);
      break;

    case ID_FILE_QUIT:
      ui_request_quit();
      break;

    case ID_TOOL_SELECT:
    case ID_TOOL_MOVE:
    case ID_TOOL_ROTATE:
    case ID_TOOL_SCALE:
      if (doc) {
        doc->scene.createMode = 0;
        int mode = id == ID_TOOL_SELECT ? EDIT_Q_SELECT
                 : id == ID_TOOL_MOVE   ? EDIT_W_MOVE
                 : id == ID_TOOL_ROTATE ? EDIT_E_ROTATE
                 :                        EDIT_R_SCALE;
        doc->scene.editMode = mode;
        scener_sync_tool_ui();
      }
      break;

    case ID_ANIMATION_PLAY:
      if (doc && doc->viewport_win) send_message(doc->viewport_win, kViewportTogglePlayback, 0, NULL);
      break;

    case ID_ANIMATION_REWIND:
      if (doc) {
        scene_set_time(&doc->scene, 0);
        if (doc->viewport_win) invalidate_window(doc->viewport_win);
      }
      break;

    case ID_VIEW_SHOW_GRID:
      g_app->debug_flags ^= DBG_NO_SHADOWS;
      break;

    case ID_VIEW_SHOW_SHADOWS:
      g_app->debug_flags ^= DBG_NO_SHADOWS;
      scener_sync_tool_ui();
      break;

    case ID_VIEW_TOGGLE_SSAO:
      g_app->debug_flags ^= DBG_SSAO;
      scener_sync_tool_ui();
      break;

    case ID_VIEW_SHOW_WIREFRAME:
      g_app->debug_flags ^= DBG_WIRE_SHADOWVOL;
      break;

    case ID_VIEW_NEXT_CAMERA:
    case ID_VIEW_PREV_CAMERA:
      if (doc && doc->scene.ncameras > 0) {
        int idx = -1;
        for (int i = 0; i < doc->scene.ncameras; i++) {
          if (!strcmp(doc->scene.cameras[i].name, doc->scene.activeCamera)) {
            idx = i; break;
          }
        }
        if (idx < 0) idx = 0;
        else if (id == ID_VIEW_NEXT_CAMERA) idx = (idx + 1) % doc->scene.ncameras;
        else idx = (idx + doc->scene.ncameras - 1) % doc->scene.ncameras;
        scene_select_camera(&doc->scene, doc->scene.cameras[idx].name);
        scener_sync_viewport_camera(doc);
        scener_sync_tool_ui();
      }
      break;

    case ID_CREATE_BOX:
    case ID_CREATE_RECT:
    case ID_CREATE_CIRCLE:
    case ID_CREATE_ELLIPSE:
    case ID_CREATE_STAR:
    case ID_CREATE_SCREEN:
    case ID_CREATE_SPHERE:
    case ID_CREATE_CYLINDER:
    case ID_CREATE_CONE:
    case ID_CREATE_TORUS:
    case ID_CREATE_PRISM:
    case ID_CREATE_CAPSULE:
    case ID_CREATE_ARCH:
    case ID_CREATE_WINDOW_ROUND:
    case ID_CREATE_WINDOW_COTTAGE:
    case ID_CREATE_WINDOW_GOTHIC:
    case ID_CREATE_DOOR_RECTANGULAR:
    case ID_CREATE_DOOR_ROUND:
    case ID_CREATE_DOOR_GOTHIC:
      if (doc) {
        doc->scene.createMode = id;
        doc->scene.editMode = EDIT_Q_SELECT;
        scener_select_panel_tab(SCENER_TAB_CREATE);
        scener_sync_tool_ui();
      }
      break;

    case ID_CREATE_POINT_LIGHT:
    case ID_CREATE_DIRECTIONAL_LIGHT: {
      if (doc) {
        Light lt = {0};
        lt.color = v3(1,1,1);
        lt.intensity = 1.0f;
        lt.castsShadow = 1;
        if (id == ID_CREATE_DIRECTIONAL_LIGHT) {
          lt.dir = vnorm(vsub(doc->scene.camLook, doc->scene.camPos));
          lt.isDirectional = 1;
        } else {
          lt.pos = doc->scene.camPos;
          lt.radius = 10.0f;
        }
        DA_PUSH(doc->scene.lights, doc->scene.nlights, doc->scene.clights, lt);
        free(doc->scene.svols);
        doc->scene.svols = calloc((size_t)doc->scene.nlights, sizeof(ShadowVolume));
        scene_build_all_shadow_volumes(&doc->scene);
        doc->modified = true;
        if (doc->viewport_win) invalidate_window(doc->viewport_win);
      }
      break;
    }

    case ID_CREATE_CAMERA:
      if (doc) {
        Camera cam = {0};
        snprintf(cam.name, sizeof(cam.name), "Cam%d", doc->scene.ncameras + 1);
        cam.pos = doc->scene.camPos;
        cam.look = doc->scene.camLook;
        cam.fov = doc->scene.camFov > 0 ? doc->scene.camFov : 60;
        DA_PUSH(doc->scene.cameras, doc->scene.ncameras, doc->scene.ccameras, cam);
        doc->modified = true;
        scener_sync_command_state();
      }
      break;

    case ID_WINDOW_COMMAND_PANEL:
      if (!g_app->command_panel_win)
        g_app->command_panel_win = create_command_panel_window();
      if (g_app->command_panel_win) {
        show_window(g_app->command_panel_win, true);
        scener_select_panel_tab(SCENER_TAB_HIERARCHY);
        property_browser_refresh();
      }
      break;

    default:
      break;
  }
}
