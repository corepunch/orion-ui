#version 150 core

out vec4 outColor;

uniform sampler2D tex0;
uniform float u_mix;
in vec2 tex;

void main(){
  vec4 c = texture(tex0, tex);
  vec3 col = c.rgb;
  col = mix(col, col * vec3(0.9, 0.85, 1.0) + vec3(0.04,0.0,0.06), 0.7);
  float lum = dot(col, vec3(0.299,0.587,0.114));
  col = mix(col, vec3(lum)*0.9+0.05, 0.2);
  col = clamp(col, 0.0, 1.0);
  col = mix(c.rgb, col, u_mix);
  outColor = vec4(col, c.a);
}
