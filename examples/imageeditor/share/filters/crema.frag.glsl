#version 150 core

out vec4 outColor;

uniform sampler2D tex0;
uniform float u_mix;
in vec2 tex;

void main(){
  vec4 c = texture(tex0, tex);
  vec3 col = c.rgb;
  col = mix(col, col * vec3(1.0, 0.97, 0.88) + vec3(0.05, 0.03, 0.02), 0.6);
  col = mix(col, vec3(dot(col, vec3(0.299,0.587,0.114))), 0.08);
  col = col * 0.92 + 0.06;
  col = clamp(col, 0.0, 1.0);
  col = mix(c.rgb, col, u_mix);
  outColor = vec4(col, c.a);
}
