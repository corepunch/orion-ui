#version 150 core

out vec4 outColor;

uniform sampler2D tex0;
uniform float u_mix;
in vec2 tex;

void main(){
  vec4 c = texture(tex0, tex);
  vec3 col = c.rgb;
  vec2 uv = tex - 0.5;
  float vign = 1.0 - dot(uv, uv) * 1.4;
  col *= vign;
  col = col * vec3(1.06, 1.0, 0.88);
  col = mix(col, col*col*(3.0-2.0*col), 0.2);
  col = clamp(col, 0.0, 1.0);
  col = mix(c.rgb, col, u_mix);
  outColor = vec4(col, c.a);
}
