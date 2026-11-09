/* Binary glTF 2.0 exporter plugin: one .glb with PBR base-colour materials. */
#include "../../exporter.h"
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

static const scener_option_t options[] = { { "merge", "Put all objects in one mesh", NULL, "0" } };
static const char *const extensions[] = { ".glb", NULL };

typedef struct { char *s; size_t n, cap; } text_t;
typedef struct { uint8_t *d; size_t n, cap; } bytes_t;

static bool text_add(text_t *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static bool text_add(text_t *t, const char *fmt, ...) {
  va_list ap; va_start(ap, fmt); int need = vsnprintf(NULL, 0, fmt, ap); va_end(ap);
  if (need < 0) return false;
  if (t->n + need + 1 > t->cap) { size_t cap = (t->cap ? t->cap * 2 : 4096); while (cap < t->n + need + 1) cap *= 2; char *s = realloc(t->s, cap); if (!s) return false; t->s = s; t->cap = cap; }
  va_start(ap, fmt); vsnprintf(t->s + t->n, need + 1, fmt, ap); va_end(ap); t->n += need;
  return true;
}

static bool text_json_string(text_t *t, const char *v) {
  if (!text_add(t, "\"")) return false;
  for (; *v; v++) { unsigned char c = (unsigned char)*v; if (c == '"' || c == '\\') { if (!text_add(t, "\\%c", c)) return false; } else if (c < 0x20) { if (!text_add(t, "\\u%04x", c)) return false; } else if (!text_add(t, "%c", c)) return false; }
  return text_add(t, "\"");
}

static bool bytes_add(bytes_t *b, const void *src, size_t n) {
  if (b->n + n > b->cap) { size_t cap = b->cap ? b->cap * 2 : 65536; while (cap < b->n + n) cap *= 2; uint8_t *d = realloc(b->d, cap); if (!d) return false; b->d = d; b->cap = cap; }
  memcpy(b->d + b->n, src, n); b->n += n; return true;
}

static void le32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static bool bytes_f32(bytes_t *b, float v) { uint32_t u; memcpy(&u, &v, 4); uint8_t p[4]; le32(p, u); return bytes_add(b, p, 4); }
static bool bytes_u32(bytes_t *b, uint32_t v) { uint8_t p[4]; le32(p, v); return bytes_add(b, p, 4); }

static float linear(float c) { return powf(fminf(fmaxf(c, 0.f), 1.f), 2.2f); }
static float roughness(float shininess) { return fminf(fmaxf(1.f - sqrtf(fminf(fmaxf(shininess, 0.f), 128.f) / 128.f), .1f), 1.f); }

static size_t material_index(const scener_export_scene_t *s, size_t i) {
  const scener_export_object_t *o = &s->objects[i];
  for (size_t j = 0; j < i; j++) { const scener_export_object_t *p = &s->objects[j]; if (p->color[0] == o->color[0] && p->color[1] == o->color[1] && p->color[2] == o->color[2] && p->shininess == o->shininess) return material_index(s, j); }
  size_t index = 0;
  for (size_t j = 0; j < i; j++) { bool seen = false; const scener_export_object_t *p = &s->objects[j]; for (size_t k = 0; k < j && !seen; k++) { const scener_export_object_t *q = &s->objects[k]; seen = p->color[0] == q->color[0] && p->color[1] == q->color[1] && p->color[2] == q->color[2] && p->shininess == q->shininess; } if (!seen) index++; }
  return index;
}

static bool export_scene(const char *path, const scener_export_scene_t *scene, const scener_export_options_t *opts) {
  bool merge = !strcmp(scener_option(opts, "merge", "0"), "1"), ok = false;
  text_t js = {0}; bytes_t bin = {0}; size_t nviews = 0, nmaterials = 0;
  text_t views = {0}, accessors = {0}, meshes = {0}, nodes = {0}, materials = {0};
  for (size_t i = 0; i < scene->nobjects; i++) if (material_index(scene, i) == nmaterials) {
    const scener_export_object_t *o = &scene->objects[i];
    ok = text_add(&materials, "%s{\"pbrMetallicRoughness\":{\"baseColorFactor\":[%.5f,%.5f,%.5f,1],\"metallicFactor\":0,\"roughnessFactor\":%.3f}}", nmaterials ? "," : "", linear(o->color[0]), linear(o->color[1]), linear(o->color[2]), roughness(o->shininess));
    if (!ok) goto done;
    nmaterials++;
  }
  ok = false;
  if (merge) text_add(&meshes, "{\"name\":\"scene\",\"primitives\":[");
  for (size_t i = 0; i < scene->nobjects; i++) {
    const scener_export_object_t *o = &scene->objects[i]; float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    size_t first = nviews;
    for (int part = 0; part < 4; part++) {
      size_t start = bin.n, count = part == 3 ? o->ntris * 3 : o->nverts, comps = part == 1 || part == 0 ? 3 : part == 2 ? 2 : 1;
      for (size_t k = 0; k < (part == 3 ? o->ntris * 3 : o->nverts * comps); k++) {
        bool good;
        if (part == 0) { float v = o->positions[k]; lo[k % 3] = fminf(lo[k % 3], v); hi[k % 3] = fmaxf(hi[k % 3], v); good = bytes_f32(&bin, v); }
        else if (part == 1) good = bytes_f32(&bin, o->normals[k]);
        else if (part == 2) good = bytes_f32(&bin, k % 2 ? 1.f - o->uvs[k] : o->uvs[k]);
        else good = bytes_u32(&bin, o->indices[k]);
        if (!good) goto done;
      }
      text_add(&views, "%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"target\":%d}", nviews ? "," : "", start, bin.n - start, part == 3 ? 34963 : 34962);
      const char *type = part == 3 ? "SCALAR" : part == 2 ? "VEC2" : "VEC3";
      text_add(&accessors, "%s{\"bufferView\":%zu,\"componentType\":%d,\"count\":%zu,\"type\":\"%s\"", nviews ? "," : "", nviews, part == 3 ? 5125 : 5126, count, type);
      if (part == 0) text_add(&accessors, ",\"min\":[%.7g,%.7g,%.7g],\"max\":[%.7g,%.7g,%.7g]", lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
      text_add(&accessors, "}");
      nviews++;
    }
    text_add(&meshes, "%s", i ? "," : "");
    if (!merge) { text_add(&meshes, "{\"name\":"); text_json_string(&meshes, o->name); text_add(&meshes, ",\"primitives\":["); }
    text_add(&meshes, "{\"attributes\":{\"POSITION\":%zu,\"NORMAL\":%zu,\"TEXCOORD_0\":%zu},\"indices\":%zu,\"material\":%zu}", first, first + 1, first + 2, first + 3, material_index(scene, i));
    if (!merge) text_add(&meshes, "]}");
    if (!merge) { text_add(&nodes, "%s{\"name\":", i ? "," : ""); text_json_string(&nodes, o->name); text_add(&nodes, ",\"mesh\":%zu}", i); }
  }
  if (merge) { text_add(&meshes, "]}"); text_add(&nodes, "{\"name\":\"scene\",\"mesh\":0}"); }
  size_t nnodes = merge ? 1 : scene->nobjects;
  text_add(&js, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Scener\"},\"scene\":0,\"scenes\":[{\"nodes\":[");
  for (size_t i = 0; i < nnodes; i++) text_add(&js, "%s%zu", i ? "," : "", i);
  text_add(&js, "]}],\"nodes\":[%s],\"meshes\":[%s],\"materials\":[%s],\"accessors\":[%s],\"bufferViews\":[%s],\"buffers\":[{\"byteLength\":%zu}]}", nodes.s, meshes.s, materials.s, accessors.s, views.s, bin.n);
  if (!js.s) goto done;
  {
    size_t jpad = (4 - js.n % 4) % 4, bpad = (4 - bin.n % 4) % 4, total = 12 + 8 + js.n + jpad + 8 + bin.n + bpad;
    uint8_t head[20]; static const uint8_t zeros[4] = {0}; static const char spaces[4] = {' ', ' ', ' ', ' '};
    FILE *f = fopen(path, "wb");
    if (!f) { char m[1100]; snprintf(m, sizeof(m), "cannot write %s", path); scene->log(m); goto done; }
    le32(head, 0x46546C67); le32(head + 4, 2); le32(head + 8, (uint32_t)total); le32(head + 12, (uint32_t)(js.n + jpad)); le32(head + 16, 0x4E4F534A);
    ok = fwrite(head, 1, 20, f) == 20 && fwrite(js.s, 1, js.n, f) == js.n && fwrite(spaces, 1, jpad, f) == jpad;
    uint8_t bh[8]; le32(bh, (uint32_t)(bin.n + bpad)); le32(bh + 4, 0x004E4942);
    ok = ok && fwrite(bh, 1, 8, f) == 8 && fwrite(bin.d, 1, bin.n, f) == bin.n && fwrite(zeros, 1, bpad, f) == bpad;
    ok = fclose(f) == 0 && ok;
  }
done:
  if (!ok) scene->log("glb: write failed (out of memory or file error)");
  free(js.s); free(bin.d); free(views.s); free(accessors.s); free(meshes.s); free(nodes.s); free(materials.s);
  return ok;
}

static const scener_exporter_t exporter = {
  SCENER_EXPORTER_ABI, "glb", "glTF 2.0 binary", "Binary glTF with PBR base colours, one node per object",
  extensions, options, sizeof(options) / sizeof(options[0]), export_scene,
};

SCENER_EXPORTER_API const scener_exporter_t *scener_get_exporter(void) { return &exporter; }
