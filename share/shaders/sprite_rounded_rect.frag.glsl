#version 150 core

in vec2 tex;
in vec4 col;
out vec4 outColor;

uniform sampler2D tex0;
uniform vec4 tint;
uniform float alpha;
uniform vec2 size;       // window size in pixels
uniform vec4 radii;      // corner radii in pixels: top-left, top-right, bottom-right, bottom-left
uniform vec4 params0;    // shadow sigma, expanded-quad padding; sigma=0 means texture
uniform vec4 params1;    // x = source is premultiplied linear RGB

float srgb_to_linear(float x) {
  return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}

// Signed distance to a box centered at the origin with an independent radius per corner.
float roundedBoxSDF(vec2 p, vec2 b, vec4 radii) {
  float r = p.x < 0.0 ? (p.y < 0.0 ? radii.x : radii.w) : (p.y < 0.0 ? radii.y : radii.z);
  vec2 q = abs(p) - b + vec2(r);
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
  if (params0.x > 0.0) {
    vec2 p = (tex - 0.5) * (size + 2.0 * params0.y);
    float d = roundedBoxSDF(p, size * 0.5, radii);
    // Gaussian-tail approximation to a blurred silhouette, evaluated in one pass.
    float x = abs(d) / (params0.x * 1.41421356);
    float t = 1.0 / (1.0 + 0.3275911 * x);
    float tail = (((((1.061405429 * t - 1.453152027) * t) + 1.421413741)
                   * t - 0.284496736) * t + 0.254829592) * t * exp(-x * x);
    float coverage = 0.5 * tail;
    if (d < 0.0) coverage = 1.0 - coverage;
    vec3 shadow_rgb = vec3(srgb_to_linear(tint.r), srgb_to_linear(tint.g),
                           srgb_to_linear(tint.b));
    float shadow_alpha = tint.a * alpha * coverage;
    outColor = vec4(shadow_rgb * shadow_alpha, shadow_alpha);
    return;
  }
  vec4 src = texture(tex0, tex);
  vec3 tint_linear = vec3(srgb_to_linear(tint.r), srgb_to_linear(tint.g),
                          srgb_to_linear(tint.b));
  float aa = 1.0;
  if (max(max(radii.x, radii.y), max(radii.z, radii.w)) > 0.0) {
  // Map texcoord [0,1] to pixel space centered at the window center.
    vec2 pixel = tex * size;
    vec2 center = size * 0.5;
    float d = roundedBoxSDF(pixel - center, size * 0.5, radii);
    // Smooth AA: spread transition over ~1.5 pixels.
    aa = 1.0 - smoothstep(-1.5, 1.5, d);
  }
  float factor = alpha * aa * col.a * tint.a;
  vec3 straight_rgb = src.rgb * col.rgb * tint_linear;
  if (params1.x < 0.5) straight_rgb *= src.a;
  outColor = vec4(straight_rgb * factor, src.a * factor);
}
