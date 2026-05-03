#version 150 core

in vec2 tex;
in vec4 col;
out vec4 outColor;

uniform sampler2D tex0;
uniform vec4 tint;
uniform float alpha;
uniform vec4 params0;
uniform vec4 params1;

float luma_at(vec2 uv) {
  vec3 c = texture(tex0, uv).rgb;
  return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
  vec2 px = params0.xy;
  float gx = -luma_at(tex + vec2(-px.x, -px.y))
             -2.0 * luma_at(tex + vec2(-px.x, 0.0))
             -luma_at(tex + vec2(-px.x, px.y))
             +luma_at(tex + vec2(px.x, -px.y))
             +2.0 * luma_at(tex + vec2(px.x, 0.0))
             +luma_at(tex + vec2(px.x, px.y));
  float gy = -luma_at(tex + vec2(-px.x, -px.y))
             -2.0 * luma_at(tex + vec2(0.0, -px.y))
             -luma_at(tex + vec2(px.x, -px.y))
             +luma_at(tex + vec2(-px.x, px.y))
             +2.0 * luma_at(tex + vec2(0.0, px.y))
             +luma_at(tex + vec2(px.x, px.y));
  float e = clamp(length(vec2(gx, gy)), 0.0, 1.0);
  float a = texture(tex0, tex).a * alpha;
  outColor = vec4(vec3(e), a) * col * tint;
}
