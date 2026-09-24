#version 150 core

in vec2 tex;
in vec4 col;
out vec4 outColor;

uniform sampler2D tex0;
uniform vec4 tint;
uniform float alpha;
uniform vec4 params0;
uniform vec4 params1;

float srgb_to_linear(float x) {
  return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}

void main() {
  vec4 src = texture(tex0, tex) * col;
  vec4 left = params0;
  vec4 right = params1;
  left.rgb = vec3(srgb_to_linear(left.r), srgb_to_linear(left.g), srgb_to_linear(left.b));
  right.rgb = vec3(srgb_to_linear(right.r), srgb_to_linear(right.g), srgb_to_linear(right.b));
  vec4 grad = mix(left, right, clamp(tex.x, 0.0, 1.0));
  outColor = vec4(grad.rgb, grad.a * src.a * tint.a);
  outColor.a *= alpha;
}
