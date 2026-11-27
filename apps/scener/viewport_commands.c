#include "scener.h"

#define VIEWPORT_ZOOM_FACTOR 1.2f
#define VIEWPORT_FRAME_PADDING 1.15f
#define VIEWPORT_FRAME_MIN_RADIUS 0.05f
#define VIEWPORT_MIN_DISTANCE 0.05f
#define VIEWPORT_MAX_DISTANCE 10000.0f

static vec3 viewport_bounds_center(vec3 lo, vec3 hi) {
  return vscale(vadd(lo, hi), 0.5f);
}

static vec3 viewport_camera_direction(const Scene *scene) {
  vec3 dir = vsub(scene->camLook, scene->camPos);
  if (vlen(dir) < 0.001f) dir = v3(0, 0, -1);
  return vnorm(dir);
}

void scener_frame_selection(scene_doc_t *doc) {
  if (!doc) return;
  Scene *scene = &doc->scene;
  vec3 lo, hi;
  int selected = scene->selectedObj;
  if (selected >= 0 && selected < scene->nobjs && scene->objs[selected].renderable) {
    lo = scene->objs[selected].bmin;
    hi = scene->objs[selected].bmax;
  } else {
    bool has_bounds = false;
    for (int i = 0; i < scene->nobjs; i++) {
      if (scene->objs[i].renderable) { has_bounds = true; break; }
    }
    if (!has_bounds) return;
    scene_get_bounds(scene, &lo, &hi);
  }

  vec3 center = viewport_bounds_center(lo, hi);
  float radius = vlen(vscale(vsub(hi, lo), 0.5f));
  if (!isfinite(radius) || radius < VIEWPORT_FRAME_MIN_RADIUS) radius = VIEWPORT_FRAME_MIN_RADIUS;
  vec3 dir = viewport_camera_direction(scene);
  float aspect = 1.0f;
  if (doc->viewport_win) {
    irect16_t rect = get_client_rect(doc->viewport_win);
    if (rect.w > 0 && rect.h > 0) aspect = (float)rect.w / (float)rect.h;
  }
  float fov = scene->camFov > 1.0f && scene->camFov < 179.0f ? scene->camFov : 60.0f;
  float half_vertical = fov * M_PIf / 360.0f;
  float half_horizontal = atanf(tanf(half_vertical) * aspect);
  float half_angle = half_vertical < half_horizontal ? half_vertical : half_horizontal;
  float distance = radius * VIEWPORT_FRAME_PADDING / sinf(half_angle);
  if (!isfinite(distance) || distance < VIEWPORT_MIN_DISTANCE) distance = 1.0f;
  scene->camLook = center;
  scene->camPos = vsub(center, vscale(dir, distance));
  scener_sync_viewport_camera(doc);
}

void scener_zoom_view(scene_doc_t *doc, bool zoom_in) {
  if (!doc) return;
  Scene *scene = &doc->scene;
  vec3 look = scene->camLook;
  vec3 offset = vsub(scene->camPos, look);
  float distance = vlen(offset);
  if (!isfinite(distance) || distance < VIEWPORT_MIN_DISTANCE) {
    vec3 dir = viewport_camera_direction(scene);
    offset = vscale(dir, -1.0f);
    distance = 1.0f;
  }
  float factor = zoom_in ? 1.0f / VIEWPORT_ZOOM_FACTOR : VIEWPORT_ZOOM_FACTOR;
  distance = fmaxf(VIEWPORT_MIN_DISTANCE, fminf(VIEWPORT_MAX_DISTANCE, distance * factor));
  scene->camPos = vadd(look, vscale(vnorm(offset), distance));
  scener_sync_viewport_camera(doc);
}
