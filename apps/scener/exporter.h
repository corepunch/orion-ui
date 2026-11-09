#ifndef __SCENER_EXPORTER_H__
#define __SCENER_EXPORTER_H__

/* Scener exporter plugin ABI, version 1.
 *
 * An exporter is a shared library (.dylib, .so or .dll) that exports one symbol:
 *
 *   SCENER_EXPORTER_API const scener_exporter_t *scener_get_exporter(void);
 *
 * Like a 3ds Max SceneExport plugin it declares its file extensions, descriptions
 * and options, and writes the file itself. It depends on nothing in Scener or Orion:
 * the scene is handed over as plain read-only arrays that stay valid only during
 * export_scene(). Geometry is in metres, Y up, counter-clockwise front faces. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SCENER_EXPORTER_ABI 1

#if defined(_WIN32)
#  define SCENER_EXPORTER_API __declspec(dllexport)
#else
#  define SCENER_EXPORTER_API __attribute__((visibility("default")))
#endif

typedef struct {
  const char *name;                 /* authored node name, unique within the scene */
  float color[3], shininess;        /* RGB 0..1 as authored (display values); shininess as authored */
  size_t nverts, ntris;
  const float *positions;           /* 3 per vertex */
  const float *normals;             /* 3 per vertex */
  const float *uvs;                 /* 2 per vertex */
  const uint32_t *indices;          /* 3 per triangle */
} scener_export_object_t;

typedef struct {
  uint32_t abi;                     /* SCENER_EXPORTER_ABI of the host */
  const char *source;               /* path of the exported scene */
  char authored_up;                 /* 'y' or 'z': the axis the author treats as up */
  float time;                       /* pose time in seconds */
  float bounds_min[3], bounds_max[3];
  size_t nobjects;
  const scener_export_object_t *objects;
  void (*log)(const char *message); /* diagnostics to the host's stderr */
} scener_export_scene_t;

typedef struct {
  const char *id;                   /* option key, e.g. "up" */
  const char *label;                /* shown in an export dialog */
  const char *choices;              /* comma separated ("y,z"), or NULL for an on/off flag */
  const char *default_value;        /* one of choices, or "0"/"1" */
} scener_option_t;

typedef struct {
  size_t n;
  const char *const *keys, *const *values;
} scener_export_options_t;

typedef struct {
  uint32_t abi;                     /* SCENER_EXPORTER_ABI the plugin was built against */
  const char *id;                   /* "obj" */
  const char *short_desc;           /* "Wavefront OBJ" */
  const char *long_desc;
  const char *const *extensions;    /* {".obj", NULL} */
  const scener_option_t *options;
  size_t noptions;
  bool (*export_scene)(const char *path, const scener_export_scene_t *scene,
                       const scener_export_options_t *options);
} scener_exporter_t;

/* Option value for key, or fallback; plugins call this instead of parsing the table. */
static inline const char *scener_option(const scener_export_options_t *o, const char *key, const char *fallback) {
  for (size_t i = 0; o && i < o->n; i++) if (!strcmp(o->keys[i], key)) return o->values[i];
  return fallback;
}

typedef const scener_exporter_t *(*scener_get_exporter_fn)(void);

#endif
