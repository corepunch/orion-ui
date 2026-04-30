#version 150 core

out vec4 outColor;

uniform sampler2D tex0;
uniform float u_mix;
in vec2 tex;

void main(){
  vec4 c = texture(tex0, tex);
  vec3 col = c.rgb;
  col = col * vec3(1.0, 0.9, 0.85) + vec3(0.05, 0.02, 0.0);
  col = mix(col, col * col * (3.0 - 2.0*col), 0.15);
  col = clamp(col, 0.0, 1.0);
  col = mix(c.rgb, col, u_mix);
  outColor = vec4(col, c.a);
}
