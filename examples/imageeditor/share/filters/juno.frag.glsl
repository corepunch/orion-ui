#version 150 core

out vec4 outColor;

uniform sampler2D tex0;
uniform float u_mix;
in vec2 tex;

void main(){
  vec4 c = texture(tex0, tex);
  vec3 col = c.rgb;
  col *= vec3(1.04, 1.0, 0.9);
  float lum = dot(col, vec3(0.299,0.587,0.114));
  col += (1.0 - col) * (1.0 - lum) * 0.12;
  col = clamp(col, 0.0, 1.0);
  col = pow(col, vec3(0.95));
  col = mix(c.rgb, col, u_mix);
  outColor = vec4(col, c.a);
}
