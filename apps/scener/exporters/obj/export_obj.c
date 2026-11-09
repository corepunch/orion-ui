/* Wavefront OBJ exporter plugin: FILE.obj plus FILE.mtl, one object group per authored node. */
#include "../../exporter.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const scener_option_t options[] = {
  { "up",    "Up axis",                    "y,z", "y" },
  { "merge", "Write one object for the scene", NULL, "0" },
};
static const char *const extensions[] = { ".obj", NULL };

static void axes(bool zup, const float *p, float *o) { if (zup) { o[0] = p[0]; o[1] = -p[2]; o[2] = p[1]; } else { o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; } }

static void material_name(const scener_export_object_t *o, char *out, size_t size) {
  snprintf(out, size, "m_%02x%02x%02x_s%d", (int)lroundf(o->color[0] * 255), (int)lroundf(o->color[1] * 255), (int)lroundf(o->color[2] * 255), (int)lroundf(o->shininess));
}

static bool write_mtl(const char *path, const scener_export_scene_t *scene) {
  FILE *f = fopen(path, "w");
  if (!f) { char m[1100]; snprintf(m, sizeof(m), "cannot write %s", path); scene->log(m); return false; }
  fprintf(f, "# Scener %s\n", scene->source);
  for (size_t i = 0; i < scene->nobjects; i++) {
    const scener_export_object_t *o = &scene->objects[i]; char name[64], other[64]; bool seen = false;
    material_name(o, name, sizeof(name));
    for (size_t j = 0; j < i && !seen; j++) { material_name(&scene->objects[j], other, sizeof(other)); seen = !strcmp(name, other); }
    if (seen) continue;
    float ns = fminf(fmaxf(o->shininess, 0.f), 1000.f);
    fprintf(f, "newmtl %s\nKd %.4f %.4f %.4f\nKa 0 0 0\nKs %.2f %.2f %.2f\nNs %.2f\nd 1\nillum 2\n\n", name, o->color[0], o->color[1], o->color[2], ns > 0 ? .3 : 0., ns > 0 ? .3 : 0., ns > 0 ? .3 : 0., ns);
  }
  return fclose(f) == 0;
}

static bool export_scene(const char *path, const scener_export_scene_t *scene, const scener_export_options_t *opts) {
  bool zup = !strcmp(scener_option(opts, "up", "y"), "z"), merge = !strcmp(scener_option(opts, "merge", "0"), "1");
  char mtl[1100]; size_t n = strlen(path);
  snprintf(mtl, sizeof(mtl), "%.*s.mtl", (int)((n > 4 && !strcmp(path + n - 4, ".obj")) ? n - 4 : n), path);
  const char *base = strrchr(mtl, '/'); const char *back = strrchr(mtl, '\\'); if (back > base) base = back; base = base ? base + 1 : mtl;
  if (!write_mtl(mtl, scene)) return false;
  FILE *f = fopen(path, "w");
  if (!f) { char m[1100]; snprintf(m, sizeof(m), "cannot write %s", path); scene->log(m); return false; }
  fprintf(f, "# Scener %s, metres, %c up\nmtllib %s\n", scene->source, zup ? 'Z' : 'Y', base);
  if (merge) fprintf(f, "o scene\n");
  size_t offset = 1;
  for (size_t i = 0; i < scene->nobjects; i++) {
    const scener_export_object_t *o = &scene->objects[i]; char mat[64];
    if (!merge) fprintf(f, "o %s\n", o->name);
    for (size_t v = 0; v < o->nverts; v++) { float p[3]; axes(zup, o->positions + 3 * v, p); fprintf(f, "v %.6g %.6g %.6g\n", p[0], p[1], p[2]); }
    for (size_t v = 0; v < o->nverts; v++) fprintf(f, "vt %.6g %.6g\n", o->uvs[2 * v], o->uvs[2 * v + 1]);
    for (size_t v = 0; v < o->nverts; v++) { float p[3]; axes(zup, o->normals + 3 * v, p); fprintf(f, "vn %.5g %.5g %.5g\n", p[0], p[1], p[2]); }
    material_name(o, mat, sizeof(mat));
    fprintf(f, "usemtl %s\n", mat);
    for (size_t t = 0; t < o->ntris; t++) {
      size_t a = o->indices[3 * t] + offset, b = o->indices[3 * t + 1] + offset, c = o->indices[3 * t + 2] + offset;
      fprintf(f, "f %zu/%zu/%zu %zu/%zu/%zu %zu/%zu/%zu\n", a, a, a, b, b, b, c, c, c);
    }
    offset += o->nverts;
  }
  return fclose(f) == 0;
}

static const scener_exporter_t exporter = {
  SCENER_EXPORTER_ABI, "obj", "Wavefront OBJ", "Triangles with normals and UVs, one group per node, plus an .mtl",
  extensions, options, sizeof(options) / sizeof(options[0]), export_scene,
};

SCENER_EXPORTER_API const scener_exporter_t *scener_get_exporter(void) { return &exporter; }
