#version 150 core

in vec2 tex;
in vec4 col;
out vec4 outColor;

uniform sampler2D tex0;
uniform vec4 tint;
uniform float alpha;
uniform vec4 params0;
uniform vec4 params1;

void main() {
  vec4 src = texture(tex0, tex) * col * tint;
  outColor = vec4(vec3(1.0) - src.rgb, src.a);
  outColor.a *= alpha;
}
