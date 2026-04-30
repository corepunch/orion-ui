#version 150 core

in vec2 tex;
in vec4 col;
out vec4 outColor;

uniform sampler2D tex0;
uniform vec4 tint;
uniform float alpha;
uniform vec4 params0;
uniform vec4 params1;

vec3 apply_levels(vec3 c, float in_black, float in_white, float gamma,
                  float out_black, float out_white) {
  float in_range = max(in_white - in_black, 0.0001);
  c = clamp((c - vec3(in_black)) / vec3(in_range), 0.0, 1.0);
  c = pow(c, vec3(max(gamma, 0.0001)));
  return mix(vec3(clamp(out_black, 0.0, 1.0)),
             vec3(clamp(out_white, 0.0, 1.0)),
             c);
}

void main() {
  vec4 src = texture(tex0, tex) * col * tint;
  outColor = vec4(apply_levels(src.rgb, params0.x, params0.y, params0.z,
                               params0.w, params1.x), src.a);
  outColor.a *= alpha;
}
