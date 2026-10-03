#version 150 core

in vec2 tex;
out vec4 outColor;

uniform sampler2D cellTex;
uniform sampler2D fontTex;
uniform sampler2D paletteTex;
uniform vec2 gridSize;
uniform vec2 cellSize;   // (glyph_cell_w, glyph_cell_h) in pixels

#define ATLAS_COLS 256.0

void main() {
  vec2 g = tex * gridSize;
  vec2 cell = floor(g);
  vec2 fracCell = fract(g);
  vec2 cellUv = (cell + vec2(0.5)) / gridSize;
  vec4 cell_data = texture(cellTex, cellUv);
  // R+G = 16-bit glyph index, B = fg, A = bg
  float ch = floor(cell_data.r * 255.0 + 0.5) + floor(cell_data.g * 255.0 + 0.5) * 256.0;
  int fg = int(floor(cell_data.b * 255.0 + 0.5));
  int bg = int(floor(cell_data.a * 255.0 + 0.5));
  float col = mod(ch, ATLAS_COLS);
  float row = floor(ch / ATLAS_COLS);
  float px = floor(fracCell.x * cellSize.x);
  float py = floor(fracCell.y * cellSize.y);
  vec2 sheetSize = cellSize * ATLAS_COLS;
  vec2 fuv = (vec2(col * cellSize.x + px,
                   row * cellSize.y + py) + vec2(0.5)) / sheetSize;
  float a = texture(fontTex, fuv).a;
  vec4 bgColor = texture(paletteTex, vec2((float(bg) + 0.5) / 256.0, 0.5));
  vec4 fgColor = texture(paletteTex, vec2((float(fg) + 0.5) / 256.0, 0.5));
  outColor = mix(bgColor, fgColor, a);
}
