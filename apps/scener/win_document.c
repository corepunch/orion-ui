#include "scener.h"

#define PREFAB_FRAME_MIN_RADIUS 0.5f
#define PREFAB_FRAME_OFFSET_X   1.2f
#define PREFAB_FRAME_OFFSET_Y   0.7f
#define PREFAB_FRAME_OFFSET_Z   2.4f

static void doc_frame_prefab(Scene *scene) {
	vec3 bmin,bmax; scene_get_bounds(scene,&bmin,&bmax);
	vec3 center=vscale(vadd(bmin,bmax),0.5f);
	float radius=vlen(vscale(vsub(bmax,bmin),0.5f));
	if(radius<PREFAB_FRAME_MIN_RADIUS) radius=PREFAB_FRAME_MIN_RADIUS;
	scene->camPos=vadd(center,v3(radius*PREFAB_FRAME_OFFSET_X,radius*PREFAB_FRAME_OFFSET_Y,radius*PREFAB_FRAME_OFFSET_Z));
	scene->camLook=center;
	if(scene->ncameras){ scene->cameras[0].pos=scene->camPos; scene->cameras[0].look=scene->camLook; }
}

static irect16_t document_workspace_rect(void) {
	irect16_t area = R(0, 0, ui_get_system_metrics(kSystemMetricScreenWidth),
		ui_get_system_metrics(kSystemMetricScreenHeight));
	get_application_workspace(g_app->hinstance, &area);
#ifdef BUILD_AS_GEM
	area = rect_trim_right(area, SIDE_PANEL_WIDTH);
#endif
	return rect_inset(area, 6);
}

static void doc_win_resize_children(scene_doc_t *doc) {
  if (!doc || !doc->win) return;
  irect16_t cr = get_client_rect(doc->win);
  if (doc->viewport_win)
    resize_window(doc->viewport_win, cr.w, cr.h);
}

result_t doc_win_proc(window_t *win, uint32_t msg,
                      uint32_t wparam, void *lparam) {
  scene_doc_t *doc = (scene_doc_t *)win->userdata;

  switch (msg) {
    case evCreate:
      return true;

    case evSetFocus:
      if (g_app && doc) { g_app->active_doc = doc; scener_sync_tool_ui(); property_browser_refresh(); }
      return true;

    case evResize:
      doc_win_resize_children(doc);
      return false;

    case evKeyDown:
      if (ui_get_mod_state() & (AX_MOD_SHIFT | AX_MOD_CTRL | AX_MOD_ALT | AX_MOD_CMD)) return false;
      switch ((int)wparam) {
        case AX_KEY_Q: handle_menu_command(ID_TOOL_SELECT); return true;
        case AX_KEY_W: handle_menu_command(ID_TOOL_MOVE);   return true;
        case AX_KEY_E: handle_menu_command(ID_TOOL_ROTATE); return true;
        case AX_KEY_R: handle_menu_command(ID_TOOL_SCALE);  return true;
        default: return false;
      }

    case evDestroy:
      if (g_app && g_app->active_doc == doc)
        g_app->active_doc = NULL;
      return false;

    default:
      return false;
  }
}

scene_doc_t *create_document_ex(const char *path, bool show_windows) {
  scene_doc_t *doc = calloc(1, sizeof(scene_doc_t));
  if (!doc) return NULL;

  memset(&doc->scene, 0, sizeof(Scene));
  doc->scene.camFov = 60;
  doc->scene.camPos = v3(3, 2, 5);
  doc->scene.camLook = v3(0, 0, 0);
  doc->scene.ambient = v3(0.15f, 0.15f, 0.15f);
  doc->scene.bg = v3(0.25f, 0.3f, 0.35f);
  doc->scene.editMode = EDIT_W_MOVE;
  doc->scene.selectedObj = -1;

  if (path && path[0]) {
    if (!load_scene(path, &doc->scene)) {
      free(doc);
      return NULL;
    }
    if (doc->scene.prefabDocument) doc_frame_prefab(&doc->scene);
    strncpy(doc->filename, path, sizeof(doc->filename)-1);
    doc->filename[sizeof(doc->filename)-1] = '\0';
  } else {
    Light sun = {0};
    sun.dir = v3(-0.4f, -0.8f, -0.3f);
    sun.color = v3(1, 0.95f, 0.9f);
    sun.intensity = 1.0f;
    sun.isDirectional = 1;
    sun.castsShadow = 1;
    DA_PUSH(doc->scene.lights, doc->scene.nlights, doc->scene.clights, sun);
    doc->scene.svols = calloc((size_t)doc->scene.nlights, sizeof(ShadowVolume));

    Mesh floor = gen_box(10, 0.1f, 10);
    scene_add_obj(&doc->scene, floor, mat4_identity(), mat4_identity(),
                  v3(0.5f, 0.5f, 0.5f), 16, 1, 1, 0);
  }

  scene_build_all_shadow_volumes(&doc->scene);
  scene_init_textures(&doc->scene);

  if (show_windows) {
    irect16_t workspace = document_workspace_rect();
    set_default_window_position(workspace.x, workspace.y);

    window_t *dwin = create_window(
        path ? path : "Untitled",
        WINDOW_STATUSBAR,
        MAKERECT(CW_USEDEFAULT, CW_USEDEFAULT, workspace.w, workspace.h),
        NULL, doc_win_proc, g_app->hinstance, NULL);
    if (!dwin) {
      scene_free_textures(&doc->scene);
      scene_free(&doc->scene);
      free(doc);
      return NULL;
    }
    dwin->userdata = doc;
    doc->win = dwin;
    doc->viewport_win = create_viewport_window(dwin, doc);
    show_window(dwin, true);
  }

  doc->next = g_app->docs;
  g_app->docs = doc;
  g_app->active_doc = doc;
  property_browser_refresh();
  scener_sync_command_state();

  doc_update_title(doc);
  return doc;
}

scene_doc_t *create_document(const char *path) {
  return create_document_ex(path, true);
}

void close_document(scene_doc_t *doc) {
  if (!doc) return;

  if (g_app->docs == doc) {
    g_app->docs = doc->next;
  } else {
    for (scene_doc_t *d = g_app->docs; d; d = d->next) {
      if (d->next == doc) { d->next = doc->next; break; }
    }
  }

  if (g_app->active_doc == doc) g_app->active_doc = g_app->docs;
  if (doc->win) destroy_window(doc->win);
  scene_free_textures(&doc->scene);
  scene_free(&doc->scene);
  free(doc);
  property_browser_refresh();
  scener_sync_command_state();
}

bool scener_open_file_path(const char *path) {
  if (!g_app || !path || !path[0]) return false;
  scene_doc_t *doc = create_document_ex(path, true);
  return doc != NULL;
}

void doc_update_title(scene_doc_t *doc) {
  if (!doc || !doc->win) return;
  if (doc->filename[0]) {
    const char *name = strrchr(doc->filename, '/');
    name = name ? name + 1 : doc->filename;
    snprintf(doc->win->title, sizeof(doc->win->title), "%s%s", name, doc->modified ? " *" : "");
  } else {
    snprintf(doc->win->title, sizeof(doc->win->title), "Untitled%s", doc->modified ? " *" : "");
  }
}
