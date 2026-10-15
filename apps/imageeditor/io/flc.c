#include "imageeditor.h"

#if IMAGEEDITOR_INDEXED
#include <errno.h>
#include "flc_codec.h"
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

// FLIC chunks follow Jim Kent's Autodesk Animator specification (DDJ, March 1993).
// A trailing top-level 0x7074 chunk holds Penciltest editing metadata.
// Keep it outside frames for readers that reject unknown frame subchunks.
#define FLC_META 0x7074
#define FLC_LAYERS 0x7075
#define FLC_BANK 0x7076
#define FLC_LIMIT (512u * 1024u * 1024u)
#define FLC_META_SIZE 1084

static uint16_t flc_u16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t flc_u32(const uint8_t *p) { return flc_u16(p) | (uint32_t)flc_u16(p + 2) << 16; }
static void flc_put16(uint8_t *p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
static void flc_put32(uint8_t *p, uint32_t n) { flc_put16(p, n); flc_put16(p + 2, n >> 16); }

bool flc_is_file(const char *path) {
  uint8_t h[6];
  FILE *fp = fopen(path, "rb");
  if (!fp) return false;
  bool ok = fread(h, 1, sizeof(h), fp) == sizeof(h) &&
            (flc_u16(h + 4) == 0xaf12 || flc_u16(h + 4) == 0xaf11);
  fclose(fp);
  return ok;
}

static bool flc_decode(uint16_t type, const uint8_t *p, size_t size,
                        uint8_t *pixels, int w, int h, uint32_t pal[256]) {
  const uint8_t *end = p + size;
#define FLC_NEED(n) do { if ((size_t)(end - p) < (size_t)(n)) return false; } while (0)
  if (type == 4 || type == 11) {
    FLC_NEED(2);
    int packets = flc_u16(p), index = 0; p += 2;
    while (packets--) {
      FLC_NEED(2);
      index += *p++;
      int count = *p++; if (!count) count = 256;
      if (index + count > 256) return false;
      FLC_NEED(count * 3);
      while (count--) {
        int r = *p++, g = *p++, b = *p++;
        if (type == 11) { if (r > 63 || g > 63 || b > 63) return false; r <<= 2; g <<= 2; b <<= 2; }
        pal[index++] = MAKE_COLOR(r, g, b, 255);
      }
    }
  } else if (type == 16) {
    FLC_NEED((size_t)w * h);
    memcpy(pixels, p, (size_t)w * h);
  } else if (type == 13) {
    memset(pixels, 0, (size_t)w * h);
  } else if (type == 15 || type == 12 || type == 7) {
    int y = 0, lines = h;
    if (type == 12) { FLC_NEED(4); y = flc_u16(p); lines = flc_u16(p + 2); p += 4; }
    if (type == 7) { FLC_NEED(2); lines = flc_u16(p); p += 2; }
    while (lines--) {
      int packets, x = 0;
      if (type == 7) {
        for (;;) {
          FLC_NEED(2);
          uint16_t op = flc_u16(p); p += 2;
          if ((op & 0xc000) == 0xc000) { y += -(int16_t)op; if (y >= h) return false; continue; }
          if (y >= h) return false;
          if ((op & 0xc000) == 0x8000) { pixels[(size_t)y * w + w - 1] = op; continue; }
          if (op & 0xc000) return false;
          packets = op; break;
        }
      } else { FLC_NEED(1); packets = *p++; }
      if (y >= h) return false;
      while (type == 15 ? x < w : packets-- > 0) {
        if (type != 15) { FLC_NEED(1); x += *p++; }
        FLC_NEED(1);
        int count = (int8_t)*p++;
        bool repeat = type == 15 ? count > 0 : count < 0;
        int unit = type == 7 ? 2 : 1;
        int length = abs(count) * unit;
        if ((type == 15 && !length) || x > w || length > w - x) return false;
        FLC_NEED(repeat ? unit : length);
        uint8_t *dst = pixels + (size_t)y * w + x;
        if (repeat) {
          for (int i = 0; i < length; i++) dst[i] = p[i % unit];
          p += unit;
        } else { memcpy(dst, p, length); p += length; }
        x += length;
      }
      y++;
    }
  } else if (type != 18 && type != FLC_META) {
    return false;
  }
#undef FLC_NEED
  return true;
}

static bool flc_read_bank(anim_timeline_t *tl, const uint8_t *p, size_t size) {
  if (size < 12) return false;
  uint32_t count = flc_u32(p), exposures = flc_u32(p + 4), next = flc_u32(p + 8);
  if (!count || count > 65535 || exposures != (uint32_t)tl->frame_count || !next || next > INT32_MAX ||
      size != 12u + count * 36u + exposures * 136u) return false;
  anim_timeline_t *bank = calloc(1, sizeof(*bank));
  if (!bank) return false;
  bank->next_cel_id = next;
  bool ok = true;
  p += 12;
  for (uint32_t i = 0; ok && i < count; i++, p += 36) {
    anim_frame_t *cel = anim_frame_new(NULL);
    if (!cel) { ok = false; break; }
    cel->id = flc_u32(p); memcpy(cel->name, p + 4, 32); cel->name[31] = 0;
    ok = cel->id && cel->id < next && anim_bank_add(bank, cel);
    if (!ok) anim_frame_free(cel);
  }
  if (ok) {
    bank->frames = calloc(exposures, sizeof(*bank->frames));
    ok = bank->frames != NULL;
  }
  for (uint32_t i = 0; ok && i < exposures; i++, p += 136) {
    anim_exposure_t *row = &bank->frames[i];
    row->cel = anim_find_cel(bank, flc_u32(p)); row->delay_ms = flc_u32(p + 4);
    memcpy(row->note, p + 8, 128); row->note[127] = 0;
    ok = row->cel && row->delay_ms > 0 && row->delay_ms <= 65535;
    bank->frame_count++;
  }
  if (ok) {
    for (int i = 0; i < tl->cel_count; i++) anim_frame_free(tl->bank[i]);
    free(tl->bank); free(tl->frames);
    tl->bank = bank->bank; tl->cel_count = bank->cel_count; tl->next_cel_id = next;
    tl->frames = bank->frames;
    bank->bank = NULL; bank->cel_count = 0; bank->frames = NULL;
  }
  anim_timeline_free(bank);
  return ok;
}

anim_timeline_t *flc_load_layers(const char *path, int *out_w, int *out_h,
                                 uint32_t out_pal[256], uint32_t *background, bool *show_bg,
                                 pencil_file_layers_t *layers) {
  FILE *fp = fopen(path, "rb");
  uint8_t header[128], *pixels = NULL, *block = NULL, *metadata = NULL, *cel_buffers = NULL, *bank_meta = NULL;
  size_t bank_size = 0; bool bank_layers = false;
  anim_timeline_t *tl = NULL;
  long layer_offset = 0;
  uint32_t layer_length = 0;
  uint8_t active_layer = IE_LAYER_PENCIL, visible_layers = 15;
  if (layers) memset(layers, 0, sizeof(*layers));
  uint32_t pal[256], editing_pal[256];
  int colors = 1;
  memset(out_pal, 0, 256 * sizeof(*out_pal));
  for (int i = 0; i < 256; i++) pal[i] = MAKE_COLOR(0, 0, 0, 255);
  if (!fp) goto fail;
  if (fread(header, 1, 128, fp) != 128) goto fail;
  uint16_t magic = flc_u16(header + 4);
  int frames = flc_u16(header + 6), w = flc_u16(header + 8), h = flc_u16(header + 10);
  uint32_t total = flc_u32(header), speed = flc_u32(header + 16);
  if (magic == 0xaf11) speed = flc_u16(header + 16) * 1000u / 70u;
  if ((magic != 0xaf12 && magic != 0xaf11) || flc_u16(header + 12) != 8 ||
      !frames || !w || !h || w > 16384 || h > 16384 || total < 128 || total > 2u * FLC_LIMIT ||
      (uint64_t)w * h * 3 > FLC_LIMIT || speed > 65535) goto fail;
  if (!speed) speed = 83;
  if (fseek(fp, 0, SEEK_END) || ftell(fp) < (long)total || fseek(fp, 128, SEEK_SET)) goto fail;
  // Find the optional editing trailer before palette remapping. It follows the ring frame.
  for (size_t pos = 128; pos < total;) {
    uint8_t chunk[16];
    if (total - pos < 16 || fread(chunk, 1, 16, fp) != 16) goto fail;
    uint32_t length = flc_u32(chunk);
    if (length < 16 || length > total - pos) goto fail;
    if (flc_u16(chunk + 4) == FLC_META && !memcmp(chunk + 8, "PTA1", 4)) {
      if (metadata || length != 16u + (uint32_t)frames * FLC_META_SIZE) goto fail;
      metadata = malloc(length - 16);
      if (!metadata || fread(metadata, 1, length - 16, fp) != length - 16) goto fail;
    } else if (flc_u16(chunk + 4) == FLC_LAYERS) {
      if (layer_offset || (memcmp(chunk + 8, "PTL5", 4) && memcmp(chunk + 8, "PTL6", 4)) || chunk[12] != IE_LAYER_COUNT ||
          chunk[13] >= IE_LAYER_COUNT || chunk[14] > 15 || chunk[15] != 0 ||
          length < 22) goto fail;
      bank_layers = !memcmp(chunk + 8, "PTL6", 4);
      layer_offset = ftell(fp); layer_length = length - 16;
      if (layer_offset < 0 || fseek(fp, layer_length, SEEK_CUR)) goto fail;
      active_layer = chunk[13]; visible_layers = chunk[14];
    } else if (flc_u16(chunk + 4) == FLC_BANK) {
      if (bank_meta || memcmp(chunk + 8, "PTB1", 4) || length < 28 || length > FLC_LIMIT) goto fail;
      bank_size = length - 16; bank_meta = malloc(bank_size);
      if (!bank_meta || fread(bank_meta, 1, bank_size, fp) != bank_size) goto fail;
    } else if (fseek(fp, length - 16, SEEK_CUR)) goto fail;
    pos += length;
  }
  const char *ext = strrchr(path, '.');
  if ((bank_layers != (bank_meta != NULL)) || (bank_meta && !layer_offset) || (layer_offset && !metadata) || (ext && !strcasecmp(ext, ".ptf") && !layer_offset)) goto fail;
  if (fseek(fp, 128, SEEK_SET)) goto fail;
  pixels = calloc((size_t)w, h);
  tl = calloc(1, sizeof(*tl));
  if (!pixels || !tl) goto fail;
  tl->next_cel_id = 1;
  tl->fps = MAX(1, (1000 + speed / 2) / speed); tl->loop = true;
  *background = IE_PAPER_COLOR; *show_bg = true;
  size_t position = 128, budget = 0;
  while (tl->frame_count < frames) {
    uint8_t fh[16];
    if (total - position < 16 || fread(fh, 1, 16, fp) != 16) goto fail;
    uint32_t size = flc_u32(fh);
    uint16_t type = flc_u16(fh + 4), chunks = flc_u16(fh + 6);
    if (size < 16 || size > total - position || size > FLC_LIMIT) goto fail;
    position += size;
    if (type == 0xf100) { if (fseek(fp, size - 16, SEEK_CUR)) goto fail; continue; }
    if (type != 0xf1fa) goto fail;
    block = malloc(size - 16 + 1);
    if (!block || fread(block, 1, size - 16, fp) != size - 16) goto fail;
    size_t pos = 0;
    const uint8_t *meta = metadata ? metadata + (size_t)tl->frame_count * FLC_META_SIZE : NULL;
    while (chunks--) {
      if (size - 16 - pos < 6) goto fail;
      uint32_t len = flc_u32(block + pos);
      uint16_t chunk = flc_u16(block + pos + 4);
      if (len < 6 || len > size - 16 - pos) goto fail;
      if (!flc_decode(chunk, block + pos + 6, len - 6, pixels, w, h, pal)) goto fail;
      pos += len;
    }
    int delay = magic == 0xaf12 ? flc_u16(fh + 8) : 0;
    anim_frame_t *frame = anim_frame_new(NULL);
    if (!frame) goto fail;
    if (!anim_bank_add(tl, frame)) { anim_frame_free(frame); goto fail; }
    int added = anim_timeline_reuse(tl, tl->frame_count - 1, frame->id);
    if (added < 0) goto fail;
    tl->active_frame = 0;
    tl->frames[added].delay_ms = delay ? delay : (int)speed;
    frame->format = IE_FRAME_FORMAT;
    if (!layer_offset) {
      frame->data_size = (size_t)w * h;
      if (!(frame->data = malloc(frame->data_size))) goto fail;
    }
    if (meta) {
      uint32_t fps = flc_u32(meta + 36), saved_delay = flc_u32(meta + 40);
      if (memcmp(meta, "PTF1", 4) || !fps || fps > 1000 || !saved_delay || saved_delay > 65535) goto fail;
      memcpy(frame->name, meta + 4, 32); frame->name[31] = 0;
      tl->frames[added].delay_ms = saved_delay;
      tl->fps = fps; tl->loop = meta[44] != 0;
      *show_bg = meta[45] != 0; *background = flc_u32(meta + 48);
      for (int i = 0; i < 256; i++) editing_pal[i] = flc_u32(meta + 60 + i * 4);
      if (tl->frame_count > 1 && memcmp(out_pal, editing_pal, sizeof(editing_pal))) goto fail;
      memcpy(out_pal, editing_pal, sizeof(editing_pal));
      if (frame->data) memcpy(frame->data, pixels, frame->data_size);
      colors = 256;
    } else {
      uint8_t map[256]; bool used[256] = {0};
      for (size_t i = 0; i < frame->data_size; i++) used[pixels[i]] = true;
      for (int i = 0; i < 256; i++) if (used[i]) {
        int j = 1;
        while (j < colors && out_pal[j] != pal[i]) j++;
        if (j == 256) { goto fail; }
        if (j == colors) out_pal[colors++] = pal[i];
        map[i] = j;
      }
      for (size_t i = 0; i < frame->data_size; i++) frame->data[i] = map[pixels[i]];
      snprintf(frame->name, sizeof(frame->name), "Frame %d", tl->frame_count);
    }
#if IMAGEEDITOR_BW
    if (!layer_offset) {
      for (size_t p = 0; p < frame->data_size; p++) frame->data[p]--;
      if (!anim_frame_compress(frame, frame->data, w, h, FRAME_FORMAT_RLE_INDEXED)) goto fail;
    }
#endif
    budget += frame->data_size;
    if (budget > FLC_LIMIT) goto fail;
    free(block); block = NULL;
  }
#if IMAGEEDITOR_BW
  if (!layer_offset) {
    // External FLICs reserve index 0; rotate indices so 255 is transparent.
    memmove(out_pal, out_pal + 1, 255 * sizeof(*out_pal));
    out_pal[255] = 0;
  }
#endif
  if (bank_meta && !flc_read_bank(tl, bank_meta, bank_size)) goto fail;
  if (layer_offset) {
    size_t n = (size_t)w * h;
    if (fseek(fp, layer_offset, SEEK_SET)) goto fail;
    cel_buffers = malloc(6 * n);
    if (!cel_buffers) goto fail;
    budget += n;
    uint32_t remaining = layer_length;
    // File deltas use two temporary frames; retained cels are independent RLE.
    for (int plane = 0; plane < 1 + 3 * tl->cel_count; plane++) {
      uint8_t ch[6];
      if (remaining < 6 || fread(ch, 1, 6, fp) != 6) goto fail;
      uint32_t size = flc_u32(ch);
      if (size < 6 || size > remaining || size > n + (n & 1) + 6) goto fail;
      block = malloc(size);
      if (!block) goto fail;
      memcpy(block, ch, 6);
      if (fread(block + 6, 1, size - 6, fp) != size - 6) goto fail;
      uint8_t *dst = pixels;
      const uint8_t *previous = NULL;
      if (plane) {
        int index = (plane - 1) / 3, layer = (plane - 1) % 3;
        dst = cel_buffers + (size_t)layer * n;
        if (index) previous = cel_buffers + (size_t)(layer + 3) * n;
      }
      if (!flc_codec_decode(block, size, dst, previous, w, h)) goto fail;
      free(block); block = NULL; remaining -= size;
      if (plane && (plane - 1) % 3 == 2) {
        int index = (plane - 1) / 3;
        const uint8_t *planes[] = {cel_buffers, cel_buffers + n, cel_buffers + 2 * n};
        if (!anim_frame_pack_cels(tl->bank[index], planes, w, h)) goto fail;
        budget += tl->bank[index]->cels_size;
        if (budget > FLC_LIMIT) goto fail;
        memcpy(cel_buffers + 3 * n, cel_buffers, 3 * n);
      }
    }
    if (remaining) goto fail;
    if (layers) {
      layers->background = malloc(n);
      if (!layers->background) goto fail;
      memcpy(layers->background, pixels, n);
      layers->active = active_layer; layers->visible = visible_layers;
    }
  }
  fclose(fp); free(pixels); free(metadata); free(cel_buffers); free(bank_meta);
  *out_w = w; *out_h = h;
  return tl;
fail:
  if (fp) fclose(fp);
  free(pixels); free(block); free(metadata); free(cel_buffers); free(bank_meta); anim_timeline_free(tl);
  return NULL;
}

anim_timeline_t *flc_load(const char *path, int *w, int *h, uint32_t palette[256],
                          uint32_t *background, bool *show_bg) {
  return flc_load_layers(path, w, h, palette, background, show_bg, NULL);
}

static bool flc_write_frame(FILE *fp, const canvas_doc_t *doc, int index, uint8_t *previous, bool first) {
  const anim_frame_t *frame = doc->anim->frames[index].cel;
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  uint8_t *composite = NULL;
  const uint8_t *pixels = doc->pixels;
  if (pencil_has_layers(doc)) {
    composite = malloc(n);
    if (!composite || !pencil_composite_frame(doc, index, composite)) { free(composite); return false; }
    pixels = composite;
  } else if (index != doc->anim->active_frame) {
    composite = malloc(n);
    if (!composite || !anim_frame_expand_indices(frame, composite, doc->canvas_w, doc->canvas_h)) { free(composite); return false; }
    pixels = composite;
  }
  if (!pixels || doc->anim->frames[index].delay_ms < 1 || doc->anim->frames[index].delay_ms > 65535) { free(composite); return false; }
  size_t size;
  uint8_t *chunk = flc_codec_encode(pixels, first ? NULL : previous, doc->canvas_w, doc->canvas_h, &size);
  if (!chunk) { free(composite); return false; }
  uint8_t header[16] = {0}, palette[778] = {0};
  flc_put32(header, 16 + (first ? sizeof(palette) : 0) + size);
  flc_put16(header + 4, 0xf1fa); flc_put16(header + 6, first ? 2 : 1); flc_put16(header + 8, doc->anim->frames[index].delay_ms);
  flc_put32(palette, sizeof(palette)); flc_put16(palette + 4, 4); flc_put16(palette + 6, 1);
  for (int i = 0; i < 256; i++) {
    uint32_t c = doc->ipal.entries[i];
    int a = COLOR_A(c), r = COLOR_R(doc->background.color), g = COLOR_G(doc->background.color), b = COLOR_B(doc->background.color);
    palette[10 + i * 3] = (COLOR_R(c) * a + r * (255 - a)) / 255;
    palette[11 + i * 3] = (COLOR_G(c) * a + g * (255 - a)) / 255;
    palette[12 + i * 3] = (COLOR_B(c) * a + b * (255 - a)) / 255;
  }
  bool ok = fwrite(header, 1, 16, fp) == 16 && (!first || fwrite(palette, 1, sizeof(palette), fp) == sizeof(palette)) &&
            fwrite(chunk, 1, size, fp) == size;
  if (ok) memcpy(previous, pixels, n);
  free(chunk); free(composite);
  return ok;
}

static bool flc_write_metadata(FILE *fp, const canvas_doc_t *doc) {
  uint8_t header[16] = {0};
  flc_put32(header, 16u + (uint32_t)doc->anim->frame_count * FLC_META_SIZE);
  flc_put16(header + 4, FLC_META); memcpy(header + 8, "PTA1", 4);
  if (fwrite(header, 1, 16, fp) != 16) return false;
  for (int f = 0; f < doc->anim->frame_count; f++) {
    const anim_frame_t *frame = doc->anim->frames[f].cel;
    uint8_t m[FLC_META_SIZE] = {0};
    memcpy(m, "PTF1", 4); memcpy(m + 4, frame->name, 32);
    flc_put32(m + 36, doc->anim->fps); flc_put32(m + 40, doc->anim->frames[f].delay_ms);
    m[44] = doc->anim->loop; m[45] = doc->background.show;
    flc_put32(m + 48, doc->background.color);
    for (int i = 0; i < 256; i++) flc_put32(m + 60 + i * 4, doc->ipal.entries[i]);
    if (fwrite(m, 1, sizeof(m), fp) != sizeof(m)) return false;
  }
  return true;
}

static bool flc_write_bank(FILE *fp, const anim_timeline_t *tl) {
  uint8_t header[28] = {0};
  flc_put32(header, 28u + tl->cel_count * 36u + tl->frame_count * 136u);
  flc_put16(header + 4, FLC_BANK); memcpy(header + 8, "PTB1", 4);
  flc_put32(header + 16, tl->cel_count); flc_put32(header + 20, tl->frame_count); flc_put32(header + 24, tl->next_cel_id);
  if (fwrite(header, 1, sizeof(header), fp) != sizeof(header)) return false;
  for (int i = 0; i < tl->cel_count; i++) {
    uint8_t record[36] = {0};
    flc_put32(record, tl->bank[i]->id); memcpy(record + 4, tl->bank[i]->name, 32);
    if (fwrite(record, 1, sizeof(record), fp) != sizeof(record)) return false;
  }
  for (int i = 0; i < tl->frame_count; i++) {
    uint8_t record[136] = {0};
    flc_put32(record, tl->frames[i].cel->id); flc_put32(record + 4, tl->frames[i].delay_ms);
    memcpy(record + 8, tl->frames[i].note, 128);
    if (fwrite(record, 1, sizeof(record), fp) != sizeof(record)) return false;
  }
  return true;
}

static bool flc_write_layers(FILE *fp, const canvas_doc_t *doc) {
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  long start = ftell(fp);
  if (start < 0) return false;
  uint8_t header[16] = {0};
  flc_put16(header + 4, FLC_LAYERS); memcpy(header + 8, "PTL6", 4);
  header[12] = IE_LAYER_COUNT; header[13] = doc->layer.active;
  for (int i = 0; i < IE_LAYER_COUNT; i++) if (doc->layer.stack[i]->visible) header[14] |= 1u << i;
  if (fwrite(header, 1, 16, fp) != 16 ||
      !flc_codec_write(fp, doc->layer.stack[0]->pixels, NULL, doc->canvas_w, doc->canvas_h)) return false;
  long cel_start = ftell(fp);
  if (cel_start < 0) return false;
  uint8_t *blank = calloc(1, n);
  if (!blank) return false;
  bool ok = true;
  for (int f = 0; ok && f < doc->anim->cel_count; f++)
    for (int layer = 1; ok && layer < IE_LAYER_COUNT; layer++) {
      const anim_frame_t *cel = doc->anim->bank[f];
      if (cel == doc->anim->frames[doc->anim->active_frame].cel)
        memcpy(blank, doc->layer.stack[layer]->pixels, n);
      else if (cel->cels) ok = anim_frame_expand_cel(cel, layer - 1, blank, doc->canvas_w, doc->canvas_h);
      else if (layer == IE_LAYER_COLOR && cel->data) ok = anim_frame_expand_indices(cel, blank, doc->canvas_w, doc->canvas_h);
      else memset(blank, layer == IE_LAYER_PENCIL ? 0 : 255, n);
      ok = ok && flc_codec_write(fp, blank, NULL, doc->canvas_w, doc->canvas_h);
      long pos = ftell(fp);
      if (pos < cel_start || (uint64_t)(pos - cel_start) + n > FLC_LIMIT) ok = false;
    }
  long end = ftell(fp);
  if (end < start || (uint64_t)(end - start) > 2u * FLC_LIMIT) ok = false;
  flc_put32(header, end - start);
  if (ok) ok = !fseek(fp, start, SEEK_SET) && fwrite(header, 1, 16, fp) == 16 && !fseek(fp, end, SEEK_SET);
  free(blank);
  return ok;
}

bool flc_save(const char *path, const canvas_doc_t *doc) {
  if (!path || !doc || !doc->anim || doc->command.before || (doc->layer.count != 1 && !pencil_has_layers(doc)) ||
      doc->anim->playing || doc->anim->fps < 1 || doc->anim->fps > 1000 ||
      doc->anim->frame_count < 1 || doc->anim->frame_count > 65535 ||
      doc->anim->active_frame < 0 || doc->anim->active_frame >= doc->anim->frame_count ||
      doc->canvas_w < 1 || doc->canvas_h < 1 || doc->canvas_w > 16384 || doc->canvas_h > 16384) {
     return false;
  }
  uint64_t n = (uint64_t)doc->canvas_w * doc->canvas_h;
  if (n * 3 > FLC_LIMIT) return false;
  uint8_t *previous = malloc((size_t)n);
  if (!previous) return false;
  char temp[1024];
  if (snprintf(temp, sizeof(temp), "%s.XXXXXX", path) >= (int)sizeof(temp)) { free(previous); return false; }
#ifdef _WIN32
  int fd = _mktemp_s(temp, sizeof(temp)) ? -1 :
           _open(temp, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
  int fd = mkstemp(temp);
#endif
  if (fd < 0) { free(previous); return false; }
  FILE *fp = fdopen(fd, "wb");
  if (!fp) { close(fd); remove(temp); free(previous); return false; }
  uint8_t header[128] = {0};
  flc_put16(header + 4, 0xaf12);
  flc_put16(header + 6, doc->anim->frame_count);
  flc_put16(header + 8, doc->canvas_w); flc_put16(header + 10, doc->canvas_h);
  flc_put16(header + 12, 8); flc_put16(header + 14, 3);
  flc_put32(header + 16, MAX(1, 1000 / doc->anim->fps));
  flc_put16(header + 38, 1); flc_put16(header + 40, 1);
  flc_put32(header + 80, 128);
  bool ok = fwrite(header, 1, 128, fp) == 128;
  for (int i = 0; ok && i < doc->anim->frame_count; i++) {
    if (i == 1) flc_put32(header + 84, ftell(fp));
    ok = flc_write_frame(fp, doc, i, previous, i == 0);
  }
  if (doc->anim->frame_count == 1) flc_put32(header + 84, ftell(fp));
  if (ok) ok = flc_write_frame(fp, doc, 0, previous, false); // Ring frame restores the start.
  if (ok) ok = flc_write_metadata(fp, doc);
  if (ok && pencil_has_layers(doc)) ok = flc_write_bank(fp, doc->anim) && flc_write_layers(fp, doc);
  long total = ftell(fp);
  if (total < 128 || (uint64_t)total > 2u * FLC_LIMIT) ok = false;
  flc_put32(header, total);
  if (ok) ok = !fseek(fp, 0, SEEK_SET) && fwrite(header, 1, 128, fp) == 128;
  if (fflush(fp)) ok = false;
#ifdef _WIN32
  if (ok && _commit(fd)) ok = false;
#else
  if (ok && fsync(fd)) ok = false;
#endif
  if (fclose(fp)) ok = false;
#ifdef _WIN32
  if (ok) ok = MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING) != 0;
#else
  if (ok) ok = rename(temp, path) == 0;
#endif
  if (!ok) remove(temp);
  free(previous);
  return ok;
}
#endif
