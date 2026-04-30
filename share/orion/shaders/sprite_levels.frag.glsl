#version 150 core

in vec2 tex;
in vec4 col;
out vec4 outColor;

uniform sampler2D tex0;
uniform vec4 tint;
uniform float alpha;
uniform vec4 params0;
uniform vec4 params1;

vec3 apply_levels(vec3 c, float black, float white, float gamma) {
  float range = max(white - black, 0.0001);
  c = clamp((c - vec3(black)) / vec3(range), 0.0, 1.0);
  return pow(c, vec3(max(gamma, 0.0001)));
}

void main() {
  vec4 src = texture(tex0, tex) * col * tint;
  outColor = vec4(apply_levels(src.rgb, params0.x, params0.y, params0.z), src.a);
  outColor.a *= alpha;
}
