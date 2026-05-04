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
  vec2 px = params0.xy;
  vec4 c = texture(tex0, tex) * 5.0;
  c -= texture(tex0, tex + vec2( px.x, 0.0));
  c -= texture(tex0, tex + vec2(-px.x, 0.0));
  c -= texture(tex0, tex + vec2(0.0,  px.y));
  c -= texture(tex0, tex + vec2(0.0, -px.y));
  outColor = clamp(c, 0.0, 1.0) * col * tint;
  outColor.a = texture(tex0, tex).a * alpha;
}
