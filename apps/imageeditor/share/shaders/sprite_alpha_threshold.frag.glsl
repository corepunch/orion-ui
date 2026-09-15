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
  // Thumbnail cleanup uses a strict alpha test: any coverage surviving the
  // blur becomes fully opaque. Preserve the source colour for line art.
  float m = a > 0.0 ? 1.0 : 0.0;
  outColor = vec4(texture(tex0, tex).rgb, m * alpha) * col * tint;
}
