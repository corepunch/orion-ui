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
  float a = texture(tex0, tex).a;
  float width = max(params0.y, 0.0001);
  float m = smoothstep(params0.x - width, params0.x + width, a);
  outColor = vec4(vec3(m), m * alpha) * col * tint;
}
