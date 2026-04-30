#version 150 core

out vec4 outColor;

uniform sampler2D tex0;
uniform float u_mix;
in vec2 tex;

void main(){
  vec4 c = texture(tex0, tex);
  vec3 col = c.rgb;
  col = col * vec3(1.02, 1.03, 0.95);
  col = mix(col, col * col * (3.0 - 2.0*col), 0.2);
  col = clamp(col + vec3(0.04, 0.03, -0.04), 0.0, 1.0);
  col = mix(c.rgb, col, u_mix);
  outColor = vec4(col, c.a);
}
