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
  vec4 left = params0;
  vec4 right = params1;
  vec4 grad = mix(left, right, clamp(tex.x, 0.0, 1.0));
  outColor = vec4(grad.rgb, grad.a * src.a);
  outColor.a *= alpha;
}
