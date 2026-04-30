#version 150 core

out vec4 outColor;

uniform sampler2D tex0;
uniform float u_mix;
in vec2 tex;

void main(){
  vec4 c = texture(tex0, tex);
  vec3 col = c.rgb;
  col.r = mix(col.r, pow(col.r, 0.85), 0.5);
  col.b = mix(col.b, col.b * 1.1 + 0.05, 0.5);
  col.g = mix(col.g, col.g * 1.04, 0.5);
  col = clamp(col, 0.0, 1.0);
  col = mix(c.rgb, col, u_mix);
  outColor = vec4(col, c.a);
}
