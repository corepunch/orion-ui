#version 150 core

in vec2 tex;
in vec4 col;
out vec4 outColor;

uniform sampler2D tex0;
uniform vec4 tint;
uniform float alpha;
uniform vec4 params0;
uniform vec4 params1;

float linear_to_srgb(float x) {
  x = max(x, 0.0);
  return x <= 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

void main() {
  vec4 linear = texture(tex0, tex);
  vec3 rgb = linear.rgb;
  if (params0.x > 0.5)
    rgb = vec3(linear_to_srgb(linear.r), linear_to_srgb(linear.g),
               linear_to_srgb(linear.b));
  outColor = vec4(rgb, 1.0);
}
