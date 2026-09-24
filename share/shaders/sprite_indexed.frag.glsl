#version 150 core

in vec2 tex;
in vec4 col;
out vec4 outColor;
uniform sampler2D tex0;
uniform sampler2D palette_tex;
uniform float alpha;

void main() {
  float index = floor(texture(tex0, tex).a * 255.0 + 0.5);
  outColor = texture(palette_tex, vec2((index + 0.5) / 256.0, 0.5)) * col;
  outColor.rgb *= alpha;
  outColor.a *= alpha;
}
