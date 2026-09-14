#version 150 core

in vec2 tex;
in vec4 col;
out vec4 outColor;

uniform sampler2D tex0;
uniform vec4 tint;
uniform float alpha;
uniform vec2 size;       // window size in pixels
uniform float radius;    // corner radius in pixels
uniform vec4 params0;    // reserved

// Signed distance to a rounded box centered at the origin.
float roundedBoxSDF(vec2 p, vec2 b, float r) {
  vec2 q = abs(p) - b + vec2(r);
  return length(max(q, 0.0)) - r;
}

void main() {
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
