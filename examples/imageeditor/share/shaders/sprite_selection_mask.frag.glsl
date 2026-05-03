#version 150 core

in vec2 tex;
out vec4 outColor;

uniform sampler2D tex0;
uniform float alpha;
uniform vec4 params0; // xy = mask offset in normalized texture coordinates
uniform vec4 params1; // rgba = overlay color

void main() {
  vec2 sample_uv = tex - params0.xy;
  float mask = 1.0;
  if (sample_uv.x >= 0.0 && sample_uv.x <= 1.0 &&
      sample_uv.y >= 0.0 && sample_uv.y <= 1.0) {
    mask = texture(tex0, sample_uv).a;
  }
  outColor = vec4(params1.rgb, params1.a * alpha * mask);
  if (outColor.a < 0.01) discard;
}
