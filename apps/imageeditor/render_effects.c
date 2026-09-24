#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render_effects.h"

static uint32_t g_effect_programs[IE_RENDER_EFFECT_COUNT] = {0};

static char *replace_shader_text(const char *source, const char *needle,
                                 const char *replacement) {
  size_t source_len = strlen(source), needle_len = strlen(needle);
  size_t replacement_len = strlen(replacement), count = 0;
  for (const char *p = source; (p = strstr(p, needle)) != NULL; p += needle_len)
    count++;
  if (replacement_len > needle_len &&
      count > (SIZE_MAX - source_len - 1) / (replacement_len - needle_len))
    return NULL;
  size_t out_len = source_len + count * (replacement_len - needle_len);
  char *out = malloc(out_len + 1);
  if (!out) return NULL;
  const char *src = source;
  char *dst = out;
  while (*src) {
    const char *match = strstr(src, needle);
    if (!match) {
      size_t tail = strlen(src);
      memcpy(dst, src, tail + 1);
      break;
    }
    size_t head = (size_t)(match - src);
    memcpy(dst, src, head);
    dst += head;
    memcpy(dst, replacement, replacement_len);
    dst += replacement_len;
    src = match + needle_len;
  }
  return out;
}

char *imageeditor_prepare_effect_shader(const char *source) {
  static const char input_helpers[] =
    "\nuniform float source_premultiplied;\n"
    "uniform float output_premultiplied;\n"
    "float imageeditor_srgb_to_linear(float x) {\n"
    "  return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);\n"
    "}\n"
    "float imageeditor_linear_to_srgb(float x) {\n"
    "  x = max(x, 0.0);\n"
    "  return x <= 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1.0 / 2.4) - 0.055;\n"
    "}\n"
    "vec4 imageeditor_effect_sample(vec2 uv) {\n"
    "  vec4 c = texture(tex0, uv);\n"
    "  if (source_premultiplied > 0.5) {\n"
    "    if (c.a > 0.00001) {\n"
    "      c.rgb = vec3(imageeditor_linear_to_srgb(c.r / c.a),\n"
    "                   imageeditor_linear_to_srgb(c.g / c.a),\n"
    "                   imageeditor_linear_to_srgb(c.b / c.a));\n"
    "    } else c.rgb = vec3(0.0);\n"
    "  }\n"
    "  return c;\n"
    "}\n";
  static const char output_wrapper[] =
    "\nvoid main() {\n"
    "  imageeditor_user_main();\n"
    "  if (output_premultiplied > 0.5) {\n"
    "    float a = clamp(outColor.a, 0.0, 1.0);\n"
    "    vec3 linear_rgb = vec3(imageeditor_srgb_to_linear(outColor.r),\n"
    "                           imageeditor_srgb_to_linear(outColor.g),\n"
    "                           imageeditor_srgb_to_linear(outColor.b));\n"
    "    outColor = vec4(linear_rgb * a, a);\n"
    "  }\n"
    "}\n";
  if (!source || !strstr(source, "uniform sampler2D tex0;") ||
      !strstr(source, "out vec4 outColor;") || !strstr(source, "void main"))
    return NULL;
  char *samples = replace_shader_text(source, "texture(tex0,",
                                       "imageeditor_effect_sample(");
  if (!samples) return NULL;
  char *renamed = replace_shader_text(samples, "void main",
                                      "void imageeditor_user_main");
  free(samples);
  if (!renamed) return NULL;
  const char *uniform_end = strstr(renamed, "uniform sampler2D tex0;");
  size_t prefix_len = (size_t)(uniform_end - renamed) + strlen("uniform sampler2D tex0;");
  size_t input_len = strlen(input_helpers), source_len = strlen(renamed);
  size_t wrapper_len = strlen(output_wrapper);
  char *prepared = malloc(source_len + input_len + wrapper_len + 1);
  if (!prepared) {
    free(renamed);
    return NULL;
  }
  memcpy(prepared, renamed, prefix_len);
  memcpy(prepared + prefix_len, input_helpers, input_len);
  memcpy(prepared + prefix_len + input_len, renamed + prefix_len,
         source_len - prefix_len);
  memcpy(prepared + source_len + input_len, output_wrapper, wrapper_len + 1);
  free(renamed);
  return prepared;
}

static char *ie_read_text_file(const char *path) {
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

static char *ie_read_shader_file(const char *name) {
  char path[4096];
  snprintf(path, sizeof(path), "%s/../share/imageeditor/shaders/%s",
           ui_get_exe_dir(), name);
  return ie_read_text_file(path);
}

static bool ie_load_program_from_files(const char *fs_name, uint32_t *out_program) {
  const char *vs_name = "common.vert.glsl";
  char *vs_src = ie_read_shader_file(vs_name);
  char *fs_src = ie_read_shader_file(fs_name);
  if (!vs_src || !fs_src) {
    printf("ImageEditor shader load error: %s / %s\n", vs_name, fs_name);
    free(vs_src);
    free(fs_src);
    return false;
  }

  char *prepared_fs = imageeditor_prepare_effect_shader(fs_src);
  free(fs_src);
  if (!prepared_fs) {
    free(vs_src);
    return false;
  }
  bool ok = ui_load_program_from_source(vs_src, prepared_fs,
                                        "position", "texcoord", "color",
                                        out_program);
  free(vs_src);
  free(prepared_fs);
  return ok;
}

static uint32_t ie_program_for_effect(imageeditor_render_effect_t effect) {
  if (effect < 0 || effect >= IE_RENDER_EFFECT_COUNT)
    effect = IE_RENDER_EFFECT_COPY;
  return g_effect_programs[(int)effect];
}

static bool roundtrip_texture_rgba(int src_tex, int w, int h, uint32_t *out_tex) {
  if (!out_tex) return false;
  *out_tex = 0;

  size_t sz = (size_t)w * (size_t)h * 4;
  uint8_t *buf = malloc(sz);
  if (!buf) return false;

  bool ok = read_texture_rgba(src_tex, w, h, buf);
  if (ok) {
    *out_tex = R_CreateTextureSRGBA8(w, h, buf, R_FILTER_LINEAR, R_WRAP_CLAMP);
    ok = (*out_tex != 0);
  }
  free(buf);
  return ok;
}

bool imageeditor_render_effects_init(void) {
  static const char *kShaderFiles[IE_RENDER_EFFECT_COUNT] = {
    "sprite_copy.frag.glsl",
    "sprite_mask.frag.glsl",
    "sprite_levels.frag.glsl",
    "sprite_invert.frag.glsl",
    "sprite_threshold.frag.glsl",
    "sprite_gradient.frag.glsl",
    "sprite_blur.frag.glsl",
    "sprite_sharpen.frag.glsl",
    "sprite_edge.frag.glsl",
    "sprite_alpha_threshold.frag.glsl",
    "sprite_selection_mask.frag.glsl",
  };

  memset(g_effect_programs, 0, sizeof(g_effect_programs));
  for (int i = 0; i < IE_RENDER_EFFECT_COUNT; i++) {
    if (!ie_load_program_from_files(kShaderFiles[i], &g_effect_programs[i])) {
      imageeditor_render_effects_shutdown();
      return false;
    }
  }
  return true;
}

void imageeditor_render_effects_shutdown(void) {
  for (int i = 0; i < IE_RENDER_EFFECT_COUNT; i++) {
    if (g_effect_programs[i]) {
      ui_delete_program(g_effect_programs[i]);
      g_effect_programs[i] = 0;
    }
  }
}

void imageeditor_draw_rect_effect(int tex, int x, int y, int w, int h,
                                  imageeditor_render_effect_t effect,
                                  const ui_render_effect_params_t *params) {
  uint32_t program = ie_program_for_effect(effect);
  if (!program) return;
  draw_rect_program_params(tex, x, y, w, h, program, 1.0f, params);
}

void imageeditor_draw_rect_effect_blend(int tex, int x, int y, int w, int h,
                                        float alpha, ui_layer_blend_t blend,
                                        imageeditor_render_effect_t effect,
                                        const ui_render_effect_params_t *params) {
  uint32_t program = ie_program_for_effect(effect);
  if (!program) return;
  draw_rect_program_params_blend(tex, x, y, w, h, alpha, blend, program, 1.0f, params);
}

bool imageeditor_bake_texture_effect(int src_tex, int w, int h,
                                     imageeditor_render_effect_t effect,
                                     const ui_render_effect_params_t *params,
                                     uint32_t *out_tex) {
  uint32_t program = ie_program_for_effect(effect);
  if (!program) return false;
  return bake_texture_program_params(src_tex, w, h, program, 1.0f, params, out_tex);
}

bool imageeditor_bake_texture_blur(int src_tex, int w, int h, int radius,
                                   uint32_t *out_tex) {
  if (!out_tex) return false;
  *out_tex = 0;
  if (src_tex == 0 || w <= 0 || h <= 0)
    return false;

  radius = CLAMP(radius, 1, 16);

  ui_render_effect_params_t p = {{0}};
  uint32_t tmp = 0;
  p.f[0] = 1.0f / (float)w;
  p.f[1] = 0.0f;
  p.f[2] = (float)radius;
  if (!imageeditor_bake_texture_effect(src_tex, w, h, IE_RENDER_EFFECT_BLUR, &p, &tmp))
    return false;

  uint32_t tmp_norm = 0;
  if (!roundtrip_texture_rgba((int)tmp, w, h, &tmp_norm)) {
    R_DeleteTexture(tmp);
    return false;
  }

  p.f[0] = 0.0f;
  p.f[1] = 1.0f / (float)h;
  p.f[2] = (float)radius;
  bool ok = imageeditor_bake_texture_effect((int)tmp_norm, w, h,
                                            IE_RENDER_EFFECT_BLUR, &p, out_tex);
  R_DeleteTexture(tmp);
  R_DeleteTexture(tmp_norm);
  return ok;
}
