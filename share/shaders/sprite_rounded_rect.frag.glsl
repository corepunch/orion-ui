#version 150 core

in vec2 tex;
in vec4 col;
out vec4 outColor;

uniform sampler2D tex0;
uniform vec4 tint;
uniform float alpha;
uniform vec2 size;       // window size in pixels
uniform float radius;    // corner radius in pixels
uniform vec4 params0;    // shadow sigma, expanded-quad padding; sigma=0 means texture

// Signed distance to a rounded box centered at the origin.
float roundedBoxSDF(vec2 p, vec2 b, float r) {
  vec2 q = abs(p) - b + vec2(r);
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
  if (params0.x > 0.0) {
    vec2 p = (tex - 0.5) * (size + 2.0 * params0.y);
    float d = roundedBoxSDF(p, size * 0.5, radius);
    // Gaussian-tail approximation to a blurred silhouette, evaluated in one pass.
    float x = abs(d) / (params0.x * 1.41421356);
    float t = 1.0 / (1.0 + 0.3275911 * x);
    float tail = (((((1.061405429 * t - 1.453152027) * t) + 1.421413741)
                   * t - 0.284496736) * t + 0.254829592) * t * exp(-x * x);
    float coverage = 0.5 * tail;
    if (d < 0.0) coverage = 1.0 - coverage;
    outColor = vec4(tint.rgb, tint.a * alpha * coverage);
    return;
  }
  vec4 src = texture(tex0, tex) * col * tint;
  if (radius <= 0.0) {
    outColor = src;
    outColor.a *= alpha;
    return;
  }
  // Map texcoord [0,1] to pixel space centered at the window center.
  vec2 pixel = tex * size;
  vec2 center = size * 0.5;
  float d = roundedBoxSDF(pixel - center, size * 0.5, radius);
  // Smooth AA: spread transition over ~1.5 pixels.
  float aa = 1.0 - smoothstep(-1.5, 1.5, d);
  outColor = src;
  outColor.a *= alpha * aa;
}
