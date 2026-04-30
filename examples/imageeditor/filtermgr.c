// Instagram-style filter manager for the image editor.
//
// Loads fragment shader presets from examples/imageeditor/share/filters,
// compiles them through the shared renderer pipeline, and exposes them as a
// dynamic menu in the main menubar.

#include "imageeditor.h"

typedef struct {
  const char *filename;
  const char *label;
} filter_file_t;

static const filter_file_t kFilterFiles[] = {
  {"aden.frag.glsl",      "Aden"},
  {"brannan.frag.glsl",    "Brannan"},
  {"clarendon.frag.glsl",  "Clarendon"},
  {"crema.frag.glsl",      "Crema"},
  {"earlybird.frag.glsl",  "Earlybird"},
  {"gingham.frag.glsl",    "Gingham"},
  {"hefe.frag.glsl",       "Hefe"},
  {"hudson.frag.glsl",     "Hudson"},
  {"inkwell.frag.glsl",    "Inkwell"},
  {"juno.frag.glsl",       "Juno"},
  {"lark.frag.glsl",       "Lark"},
  {"ludwig.frag.glsl",     "Ludwig"},
  {"mayfair.frag.glsl",    "Mayfair"},
  {"moon.frag.glsl",       "Moon"},
  {"nashville.frag.glsl",  "Nashville"},
  {"normal.frag.glsl",     "Normal"},
  {"perpetua.frag.glsl",   "Perpetua"},
  {"reyes.frag.glsl",      "Reyes"},
  {"rise.frag.glsl",       "Rise"},
  {"slumber.frag.glsl",    "Slumber"},
  {"stinson.frag.glsl",    "Stinson"},
  {"valencia.frag.glsl",   "Valencia"},
  {"vesper.frag.glsl",     "Vesper"},
  {"walden.frag.glsl",     "Walden"},
};

static char *read_text_file(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) return NULL;
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }
  long sz = ftell(fp);
  if (sz < 0) {
    fclose(fp);
    return NULL;
  }
  rewind(fp);
  char *buf = malloc((size_t)sz + 1);
  if (!buf) {
    fclose(fp);
    return NULL;
  }
  size_t got = fread(buf, 1, (size_t)sz, fp);
  fclose(fp);
  buf[got] = '\0';
  return buf;
}

static char *replace_all(const char *src, const char *needle, const char *replacement) {
  if (!src || !needle || !needle[0] || !replacement) return NULL;

  size_t src_len = strlen(src);
  size_t needle_len = strlen(needle);
  size_t repl_len = strlen(replacement);

  size_t count = 0;
  for (const char *p = src; (p = strstr(p, needle)) != NULL; p += needle_len)
    count++;

  size_t out_len = src_len + count * (repl_len - needle_len);
  char *out = malloc(out_len + 1);
  if (!out) return NULL;

  char *d = out;
  const char *p = src;
  while (*p) {
    const char *m = strstr(p, needle);
    if (!m) {
      size_t tail = strlen(p);
      memcpy(d, p, tail + 1);
      break;
    }
    size_t head = (size_t)(m - p);
    memcpy(d, p, head);
    d += head;
    memcpy(d, replacement, repl_len);
    d += repl_len;
    p = m + needle_len;
  }
  return out;
}

static char *modernize_filter_source(const char *legacy_src) {
  if (!legacy_src) return NULL;

  if (strstr(legacy_src, "#version 150 core")) {
    char *copy = strdup(legacy_src);
    return copy;
  }

  char *src = strdup(legacy_src);
  if (!src) return NULL;

  const char *remove_precision = "precision mediump float;\n";
  const char *varying_old = "varying vec2 v_uv;\n";
  const char *frag_old = "gl_FragColor";
  const char *tex_old = "texture2D";

  char *tmp = replace_all(src, remove_precision, "");
  free(src);
  src = tmp;
  if (!src) return NULL;

  tmp = replace_all(src, varying_old, "in vec2 v_uv;\n");
  free(src);
  src = tmp;
  if (!src) return NULL;

  tmp = replace_all(src, frag_old, "outColor");
  free(src);
  src = tmp;
  if (!src) return NULL;

  tmp = replace_all(src, tex_old, "texture");
  free(src);
  src = tmp;
  if (!src) return NULL;

  const char *header = "#version 150 core\nout vec4 outColor;\n\n";
  size_t hdr_len = strlen(header);
  size_t body_len = strlen(src);
  char *modern = malloc(hdr_len + body_len + 1);
  if (!modern) {
    free(src);
    return NULL;
  }
  memcpy(modern, header, hdr_len);
  memcpy(modern + hdr_len, src, body_len + 1);
  free(src);
  return modern;
}

void imageeditor_free_filters(void) {
  if (!g_app) return;
  for (int i = 0; i < g_app->filter_count; i++) {
    if (g_app->filters[i].program) {
      ui_delete_program(g_app->filters[i].program);
      g_app->filters[i].program = 0;
    }
    g_app->filters[i].name[0] = '\0';
  }
  g_app->filter_count = 0;
}

bool imageeditor_load_filters(void) {
  if (!g_app) return false;

  imageeditor_free_filters();

  char dir[4096];
  snprintf(dir, sizeof(dir), "%s/../share/imageeditor/filters", ui_get_exe_dir());

  char vs_path[4096];
  snprintf(vs_path, sizeof(vs_path), "%s/../share/imageeditor/shaders/common.vert.glsl",
           ui_get_exe_dir());
  char *vs_src = read_text_file(vs_path);
  if (!vs_src) {
    IE_DEBUG("filter vertex shader load failed: %s", vs_path);
    imageeditor_sync_filter_menu();
    return false;
  }

  int count = (int)(sizeof(kFilterFiles) / sizeof(kFilterFiles[0]));
  for (int i = 0; i < count; i++) {
    char fs_path[4096];
    snprintf(fs_path, sizeof(fs_path), "%s/%s", dir, kFilterFiles[i].filename);
    char *legacy = read_text_file(fs_path);
    if (!legacy) {
      IE_DEBUG("filter load failed: %s", fs_path);
      continue;
    }

    char *frag_src = modernize_filter_source(legacy);
    free(legacy);
    if (!frag_src) {
      IE_DEBUG("filter translate failed: %s", fs_path);
      continue;
    }

    uint32_t program = 0;
    if (!ui_load_program_from_source(vs_src, frag_src,
                                     "position", "texcoord", "color",
                                     &program)) {
      IE_DEBUG("filter compile failed: %s", fs_path);
      free(frag_src);
      continue;
    }
    free(frag_src);

    if (g_app->filter_count < IMAGEEDITOR_MAX_FILTERS) {
      strncpy(g_app->filters[g_app->filter_count].name, kFilterFiles[i].label,
              sizeof(g_app->filters[g_app->filter_count].name) - 1);
      g_app->filters[g_app->filter_count].name[sizeof(g_app->filters[g_app->filter_count].name) - 1] = '\0';
      g_app->filters[g_app->filter_count].program = program;
      g_app->filter_count++;
    } else {
      ui_delete_program(program);
    }
  }

  free(vs_src);
  imageeditor_sync_filter_menu();
  return g_app->filter_count > 0;
}

bool imageeditor_apply_filter(canvas_doc_t *doc, int filter_idx) {
  if (!g_app || !doc || filter_idx < 0 || filter_idx >= g_app->filter_count)
    return false;
  if (!g_app->filters[filter_idx].program)
    return false;
  if (doc->active_layer < 0 || doc->active_layer >= doc->layer_count)
    return false;

  layer_t *lay = doc->layers[doc->active_layer];
  if (!lay || !lay->pixels) return false;

  canvas_upload(doc);
  if (!lay->tex) return false;

  size_t px_count = (size_t)doc->canvas_w * doc->canvas_h;
  uint8_t *alpha = malloc(px_count);
  if (!alpha) return false;
  for (size_t i = 0; i < px_count; i++)
    alpha[i] = lay->pixels[i * 4 + 3];

  uint32_t baked_tex = 0;
  if (!bake_texture_program((int)lay->tex, doc->canvas_w, doc->canvas_h,
                            g_app->filters[filter_idx].program, 1.0f,
                            &baked_tex)) {
    free(alpha);
    return false;
  }

  size_t sz = (size_t)doc->canvas_w * doc->canvas_h * 4;
  uint8_t *buf = malloc(sz);
  if (!buf) {
    R_DeleteTexture(baked_tex);
    free(alpha);
    return false;
  }
  if (!read_texture_rgba((int)baked_tex, doc->canvas_w, doc->canvas_h, buf)) {
    R_DeleteTexture(baked_tex);
    free(buf);
    free(alpha);
    return false;
  }
  R_DeleteTexture(baked_tex);

  for (size_t i = 0; i < px_count; i++)
    buf[i * 4 + 3] = alpha[i];
  free(alpha);

  free(lay->pixels);
  lay->pixels = buf;
  doc->pixels = lay->pixels;
  doc->canvas_dirty = true;
  doc->modified = true;
  return true;
}
