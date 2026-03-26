// Canvas operations: pixel drawing, PNG I/O, GL texture management

// ============================================================
// Canvas pixel operations
// ============================================================

void canvas_set_pixel(canvas_doc_t *doc, int x, int y, rgba_t c) {
  if (!canvas_in_bounds(x, y)) return;
  uint8_t *p = doc->pixels + ((size_t)y * CANVAS_W + x) * 4;
  p[0]=c.r; p[1]=c.g; p[2]=c.b; p[3]=c.a;
  doc->canvas_dirty = true;
  doc->modified     = true;
}

rgba_t canvas_get_pixel(const canvas_doc_t *doc, int x, int y) {
  if (!canvas_in_bounds(x, y)) return (rgba_t){0,0,0,0};
  const uint8_t *p = doc->pixels + ((size_t)y * CANVAS_W + x) * 4;
  return (rgba_t){p[0],p[1],p[2],p[3]};
}

void canvas_clear(canvas_doc_t *doc) {
  memset(doc->pixels, 0xFF, sizeof(doc->pixels));
  doc->canvas_dirty = true;
  doc->modified     = false;
}

void canvas_draw_circle(canvas_doc_t *doc, int cx, int cy, int r, rgba_t c) {
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++)
      if (dx*dx + dy*dy <= r*r)
        canvas_set_pixel(doc, cx+dx, cy+dy, c);
}

void canvas_draw_line(canvas_doc_t *doc, int x0, int y0, int x1, int y1,
                      int radius, rgba_t c) {
  int dx = abs(x1-x0), dy = abs(y1-y0);
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  while (true) {
    canvas_draw_circle(doc, x0, y0, radius, c);
    if (x0==x1 && y0==y1) break;
    int e2 = 2*err;
    if (e2 > -dy) { err -= dy; x0 += sx; }
    if (e2 <  dx) { err += dx; y0 += sy; }
  }
}

void canvas_flood_fill(canvas_doc_t *doc, int sx, int sy, rgba_t fill) {
  rgba_t target = canvas_get_pixel(doc, sx, sy);
  if (rgba_eq(target, fill)) return;

  typedef struct { int16_t x, y; } pt_t;
  int capacity = CANVAS_W * CANVAS_H;
  pt_t *queue = malloc(sizeof(pt_t) * capacity);
  if (!queue) return;

  int head = 0, tail = 0;
  queue[tail++] = (pt_t){(int16_t)sx, (int16_t)sy};
  canvas_set_pixel(doc, sx, sy, fill);

  while (head < tail) {
    pt_t cur = queue[head++];
    int nx[4] = {cur.x+1, cur.x-1, cur.x,   cur.x};
    int ny[4] = {cur.y,   cur.y,   cur.y+1, cur.y-1};
    for (int i = 0; i < 4; i++) {
      if (canvas_in_bounds(nx[i], ny[i]) &&
          rgba_eq(canvas_get_pixel(doc, nx[i], ny[i]), target) &&
          tail < capacity) {
        canvas_set_pixel(doc, nx[i], ny[i], fill);
        queue[tail++] = (pt_t){(int16_t)nx[i], (int16_t)ny[i]};
      }
    }
  }
  free(queue);
}

// ============================================================
// PNG I/O (libpng)
// ============================================================

static bool is_png(const char *path) {
  if (!path) return false;
  size_t n = strlen(path);
  if (n < 5) return false;
  const char *ext = path + n - 4;
  return (ext[0]=='.' &&
          (ext[1]=='p'||ext[1]=='P') &&
          (ext[2]=='n'||ext[2]=='N') &&
          (ext[3]=='g'||ext[3]=='G'));
}

bool png_load(const char *path, uint8_t *out_pixels);
bool png_save(const char *path, const uint8_t *pixels);

bool png_load(const char *path, uint8_t *out_pixels) {
  FILE *fp = fopen(path, "rb");
  if (!fp) return false;

  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png) { fclose(fp); return false; }

  png_infop info = png_create_info_struct(png);
  if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(fp); return false; }

  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return false;
  }

  png_init_io(png, fp);
  png_read_info(png, info);

  int w = (int)png_get_image_width(png, info);
  int h = (int)png_get_image_height(png, info);
  png_byte ct  = png_get_color_type(png, info);
  png_byte bd  = png_get_bit_depth(png, info);

  if (bd == 16) png_set_strip_16(png);
  if (ct == PNG_COLOR_TYPE_PALETTE)       png_set_palette_to_rgb(png);
  if (ct == PNG_COLOR_TYPE_GRAY && bd<8)  png_set_expand_gray_1_2_4_to_8(png);
  if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
  if (ct == PNG_COLOR_TYPE_RGB  ||
      ct == PNG_COLOR_TYPE_GRAY ||
      ct == PNG_COLOR_TYPE_PALETTE) png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
  if (ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb(png);
  png_read_update_info(png, info);

  png_bytep *rows = malloc(sizeof(png_bytep) * h);
  if (!rows) { png_destroy_read_struct(&png, &info, NULL); fclose(fp); return false; }

  png_size_t rowbytes = png_get_rowbytes(png, info);
  for (int r = 0; r < h; r++) {
    rows[r] = malloc(rowbytes);
    if (!rows[r]) {
      for (int i = 0; i < r; i++) free(rows[i]);
      free(rows);
      png_destroy_read_struct(&png, &info, NULL);
      fclose(fp);
      return false;
    }
  }

  png_read_image(png, rows);

  memset(out_pixels, 0xFF, CANVAS_H * CANVAS_W * 4);
  int copy_w = w < CANVAS_W ? w : CANVAS_W;
  int copy_h = h < CANVAS_H ? h : CANVAS_H;
  for (int row = 0; row < copy_h; row++)
    memcpy(out_pixels + row * CANVAS_W * 4, rows[row], (size_t)copy_w * 4);

  for (int r = 0; r < h; r++) free(rows[r]);
  free(rows);
  png_destroy_read_struct(&png, &info, NULL);
  fclose(fp);
  return true;
}

bool png_save(const char *path, const uint8_t *pixels) {
  FILE *fp = fopen(path, "wb");
  if (!fp) return false;

  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png) { fclose(fp); return false; }

  png_infop info = png_create_info_struct(png);
  if (!info) { png_destroy_write_struct(&png, NULL); fclose(fp); return false; }

  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return false;
  }

  png_init_io(png, fp);
  png_set_IHDR(png, info, CANVAS_W, CANVAS_H, 8,
               PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);

  for (int r = 0; r < CANVAS_H; r++)
    png_write_row(png, (png_bytep)(pixels + r * CANVAS_W * 4));

  png_write_end(png, NULL);
  png_destroy_write_struct(&png, &info);
  fclose(fp);
  return true;
}

// ============================================================
// Canvas GL texture
// ============================================================

static void canvas_upload(canvas_doc_t *doc) {
  if (!doc->canvas_tex) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, CANVAS_W, CANVAS_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, doc->pixels);
    doc->canvas_tex = tex;
  } else if (doc->canvas_dirty) {
    glBindTexture(GL_TEXTURE_2D, doc->canvas_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, CANVAS_W, CANVAS_H,
                    GL_RGBA, GL_UNSIGNED_BYTE, doc->pixels);
  }
  doc->canvas_dirty = false;
}
