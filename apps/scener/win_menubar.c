#include "scener.h"
#include <orion/user/draw.h>

result_t scener_toolbar_proc(window_t *win, uint32_t msg,
                             uint32_t wparam, void *lparam) {
  (void)lparam;
  switch (msg) {
    case evCreate:
      scener_sync_main_toolbar();
      return true;
    case tbButtonClick:
      handle_menu_command((uint16_t)wparam);
      scener_sync_main_toolbar();
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
  scener_sync_main_toolbar();
  return win;
}

void scener_sync_main_toolbar(void) {
  if (!g_app || !g_app->main_toolbar_win) return;
  window_t *toolbar = g_app->main_toolbar_win;
  int active = scener_active_tool();
  send_message(toolbar, tbSetActiveButton, (uint32_t)active, NULL);
}

void scener_sync_tool_ui(void) {
  if (!g_app) return;
  scener_sync_main_toolbar();
  if (g_app->command_panel_win) invalidate_window(g_app->command_panel_win);
  for (scene_doc_t *doc = g_app->docs; doc; doc = doc->next)
    if (doc->viewport_win) invalidate_window(doc->viewport_win);
}

result_t scener_menubar_proc(window_t *win, uint32_t msg,
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

static scene_doc_t *current_doc(void) {
  if (!g_app) return NULL;
  scene_doc_t *doc = g_app->active_doc;
  if (!doc) doc = g_app->docs;
  return doc;
}

static bool is_create_primitive(uint16_t id) {
  return id >= ID_CREATE_BOX && id <= ID_CREATE_DOOR_GOTHIC;
}

bool scener_create_primitive(scene_doc_t *doc, uint16_t id, vec3 ground_pos) {
  if (!doc || !is_create_primitive(id)) return false;
  if (id == ID_CREATE_ROUNDED_BOX || id == ID_CREATE_SCREEN) {
    if (!scene_create_promo_shape(&doc->scene, id == ID_CREATE_SCREEN ? "screen" : "rounded-box", ground_pos)) return false;
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
  float lift = 0.0f;
  switch (id) {
    case ID_CREATE_BOX:      m = gen_box(1, 1, 1); lift = 0.5f; break;
    case ID_CREATE_SPHERE:   m = gen_sphere(0.5f, 16, 16); lift = 0.5f; break;
    case ID_CREATE_CYLINDER: m = gen_cylinder(0.5f, 1, 24); lift = 0.5f; break;
    case ID_CREATE_CONE:     m = gen_cone(0.5f, 0, 1, 24); lift = 0.5f; break;
    case ID_CREATE_TORUS:    m = gen_torus(0.4f, 0.15f, 24, 12); lift = 0.15f; break;
    case ID_CREATE_PRISM:    m = gen_prism(0.5f, 1, 6); lift = 0.5f; break;
    case ID_CREATE_CAPSULE:  m = gen_capsule(0.3f, 0.6f, 12, 12); lift = 0.3f; break;
    case ID_CREATE_ARCH:     m = gen_arch(1, 1, 0.3f, 0.15f, 12, 0); lift = 0.5f; break;
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

    case ID_VIEW_SHOW_GRID:
      g_app->debug_flags ^= DBG_NO_SHADOWS;
      break;

    case ID_VIEW_SHOW_SHADOWS:
      g_app->debug_flags ^= DBG_NO_SHADOWS;
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
    case ID_CREATE_ROUNDED_BOX:
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
      }
      break;

    case ID_WINDOW_COMMAND_PANEL:
      if (g_app->command_panel_win) {
        show_window(g_app->command_panel_win, true);
      } else {
        g_app->command_panel_win = create_command_panel_window();
      }
      break;

    default:
      break;
  }
}
