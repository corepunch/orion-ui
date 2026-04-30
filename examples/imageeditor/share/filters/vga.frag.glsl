#version 150 core

in vec2 tex;
out vec4 outColor;

uniform sampler2D cellTex;
uniform sampler2D fontTex;
uniform vec2 gridSize;
uniform vec4 egaPalette[16];

void main() {
  vec2 g = tex * gridSize;
  vec2 cell = floor(g);
  vec2 fracCell = fract(g);
  vec2 cellUv = (cell + vec2(0.5)) / gridSize;
  vec2 packed = texture(cellTex, cellUv).rg;
  float ch = floor(packed.r * 255.0 + 0.5);
  float c = floor(packed.g * 255.0 + 0.5);
  int fg = int(mod(c, 16.0));
  int bg = int(floor(c / 16.0));
  float col = mod(ch, 16.0);
  float row = floor(ch / 16.0);
  float px = floor(fracCell.x * 8.0);
  float py = floor(fracCell.y * 16.0);
  vec2 fuv = vec2((col * 8.0 + px + 0.5) / 128.0,
                  (row * 16.0 + py + 0.5) / 256.0);
  float a = texture(fontTex, fuv).a;
  outColor = mix(egaPalette[bg], egaPalette[fg], a);
}
