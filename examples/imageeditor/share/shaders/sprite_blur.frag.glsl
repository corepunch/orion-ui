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
  vec2 stepv = params0.xy;
  int radius = int(clamp(params0.z, 1.0, 16.0));
  vec4 sum = vec4(0.0);
  float weight_sum = 0.0;
  float sigma = max(float(radius) * 0.5, 0.5);

  for (int i = -16; i <= 16; i++) {
    if (abs(i) > radius) continue;
    float x = float(i);
    float weight = exp(-(x * x) / (2.0 * sigma * sigma));
    sum += texture(tex0, tex + stepv * x) * weight;
    weight_sum += weight;
  }

  outColor = (sum / max(weight_sum, 0.0001)) * col * tint;
  outColor.a *= alpha;
}
