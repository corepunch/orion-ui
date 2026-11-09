#include "ansi_parser.h"
#include "vgat.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum {
  VGAT_STATE_NORMAL = 0,
  VGAT_STATE_ESC,
  VGAT_STATE_CSI,
  VGAT_STATE_OSC,
  VGAT_STATE_OSC_ESC,
};

#define VGAT_MAX(a, b) ((a) > (b) ? (a) : (b))

void vgat_parser_init(vgat_parser_t *p, const vgat_parser_callbacks_t *cbs) {
  memset(p, 0, sizeof(*p));
  p->state = VGAT_STATE_NORMAL;
  p->screen = cbs->screen;
  p->write_cell = cbs->write_cell;
  p->newline = cbs->newline;
  p->backspace = cbs->backspace;
  p->cr = cbs->cr;
  p->cursor_left = cbs->cursor_left;
  p->cursor_right = cbs->cursor_right;
  p->cursor_up = cbs->cursor_up;
  p->cursor_down = cbs->cursor_down;
  p->set_fg = cbs->set_fg;
  p->set_bg = cbs->set_bg;
  p->reset = cbs->reset;
  p->save_cursor = cbs->save_cursor;
  p->restore_cursor = cbs->restore_cursor;
  p->erase_display = cbs->erase_display;
  p->erase_line = cbs->erase_line;
  p->cursor_pos = cbs->cursor_pos;
  p->set_title = cbs->set_title;
  p->set_palette_color = cbs->set_palette_color;
  p->reset_palette = cbs->reset_palette;
  p->cur_fg = VGAT_FG_DEFAULT;
  p->cur_bg = VGAT_BG_DEFAULT;
  p->bold = false;
}

static void osc_dispatch(vgat_parser_t *p) {
  p->osc_buf[p->osc_len] = '\0';
  // Format: Ps ; Pt  — Ps is the parameter, Pt is the payload
  int ps = 0;
  char *semi = NULL;
  for (int i = 0; i < p->osc_len; i++) {
    if (p->osc_buf[i] == ';') { p->osc_buf[i] = '\0'; semi = &p->osc_buf[i + 1]; break; }
    ps = ps * 10 + (p->osc_buf[i] - '0');
  }
  if ((ps == 0 || ps == 2) && semi && p->set_title)
    p->set_title(p->userdata, semi);

  // OSC 4 ; N ; rgb:RRRR/GGGG/BBBB — set palette color N
  if (ps == 4 && semi && p->set_palette_color) {
    int idx = 0;
    for (const char *s = semi; *s >= '0' && *s <= '9'; s++)
      idx = idx * 10 + (*s - '0');
    // Find second semicolon for rgb spec
    const char *rgb = NULL;
    for (const char *s = semi; *s; s++) {
      if (*s == ';') { rgb = s + 1; break; }
    }
    if (rgb && strncmp(rgb, "rgb:", 4) == 0) {
      // Parse rgb:RRRR/GGGG/BBBB (16-bit hex per channel)
      unsigned int r = 0, g = 0, b = 0;
      if (sscanf(rgb + 4, "%x/%x/%x", &r, &g, &b) == 3) {
        uint32_t rgba = 0xFF000000u | ((uint32_t)(r >> 8) << 16) |
                        ((uint32_t)(g >> 8) << 8) | (uint32_t)(b >> 8);
        if (idx >= 0 && idx <= 255)
          p->set_palette_color(p->userdata, idx, rgba);
      }
    }
  }

  // OSC 104 — reset all palette colors to defaults
  if (ps == 104 && p->reset_palette)
    p->reset_palette(p->userdata);
}

static void csi_final(vgat_parser_t *p, char final) {
  int *args = p->params;
  int n = p->nparams;

  switch (final) {
    case 'm':
      if (n == 0) { p->cur_fg = VGAT_FG_DEFAULT; p->cur_bg = VGAT_BG_DEFAULT; p->bold = false; break; }
      ansi_apply_sgr_codes(args, n, &p->cur_fg, &p->cur_bg, VGAT_FG_DEFAULT, VGAT_BG_DEFAULT, &p->bold);
      break;
    case 'H': case 'f':  p->cursor_pos(p->screen, n >= 1 ? args[0] - 1 : 0, n >= 2 ? args[1] - 1 : 0); break;
    case 'J':            p->erase_display(p->screen, n >= 1 ? args[0] : 0);                             break;
    case 'K':            p->erase_line(p->screen, n >= 1 ? args[0] : 0);                               break;
    case 'A':            for (int i = 0; i < VGAT_MAX(1, n >= 1 ? args[0] : 1); i++) p->cursor_up(p->screen);    break;
    case 'B':            for (int i = 0; i < VGAT_MAX(1, n >= 1 ? args[0] : 1); i++) p->cursor_down(p->screen);  break;
    case 'C':            for (int i = 0; i < VGAT_MAX(1, n >= 1 ? args[0] : 1); i++) p->cursor_right(p->screen); break;
    case 'D':            for (int i = 0; i < VGAT_MAX(1, n >= 1 ? args[0] : 1); i++) p->cursor_left(p->screen);  break;
    case 's':            p->save_cursor(p->screen);    break;
    case 'u':            p->restore_cursor(p->screen); break;
    case 'c':            p->reset(p->screen);           break;
    case 'I':            for (int i = 0; i < VGAT_MAX(1, n >= 1 ? args[0] : 1); i++) p->cursor_right(p->screen); break;
    case 'Z':            for (int i = 0; i < VGAT_MAX(1, n >= 1 ? args[0] : 1); i++) p->cursor_left(p->screen);  break;
    case 'l': case 'h':
      if (p->private_marker == '?' && n >= 1 && args[0] == 25)
        p->screen->cursor_visible = final == 'h';
      break;
    case 'd':            break;  // VPA - not implemented
    default:
      break;
  }
}

void vgat_parser_feed(vgat_parser_t *p, const uint8_t *data, int len) {
  for (int i = 0; i < len; i++) {
    uint8_t c = data[i];

    switch (p->state) {
      case VGAT_STATE_NORMAL:
        if (p->utf8_remaining) {
          if ((c & 0xC0) == 0x80) {
            p->utf8_codepoint = (p->utf8_codepoint << 6) | (c & 0x3F);
            if (--p->utf8_remaining == 0) {
              p->write_cell(p->screen, p->utf8_codepoint, p->cur_fg, p->cur_bg);
            }
            break;
          }
          p->write_cell(p->screen, 0xFFFD, p->cur_fg, p->cur_bg);   // a broken sequence
          p->utf8_remaining = 0;
        }
        if (c == 0x1B)   { p->state = VGAT_STATE_ESC; }
        else if (c == '\n') { p->newline(p->screen); }
        else if (c == '\r') { p->cr(p->screen); }
        else if (c == 0x08) { p->backspace(p->screen); }
        else if (c == '\t') { p->write_cell(p->screen, c, p->cur_fg, p->cur_bg); }
        else if (c == 0x07) { /* BEL - ignore */ }
        else if (c >= 0xF0 && c <= 0xF4) { p->utf8_codepoint = c & 0x07; p->utf8_remaining = 3; }
        else if (c >= 0xE0 && c <= 0xEF) { p->utf8_codepoint = c & 0x0F; p->utf8_remaining = 2; }
        else if (c >= 0xC2 && c <= 0xDF) { p->utf8_codepoint = c & 0x1F; p->utf8_remaining = 1; }
        else if (c >= 0x20 && c < 0x80)  { p->write_cell(p->screen, c, p->cur_fg, p->cur_bg); }
        else if (c >= 0x80)              { p->write_cell(p->screen, 0xFFFD, p->cur_fg, p->cur_bg); }
        break;

      case VGAT_STATE_ESC:
        if (c == '[')   { p->state = VGAT_STATE_CSI; p->nparams = 0; p->params[0] = 0; p->private_marker = 0; }
        else if (c == ']')  { p->state = VGAT_STATE_OSC; p->osc_len = 0; }
        else if (c == 'c')  { p->reset(p->screen); p->state = VGAT_STATE_NORMAL; }
        else if (c == '7')  { p->save_cursor(p->screen); p->state = VGAT_STATE_NORMAL; }
        else if (c == '8')  { p->restore_cursor(p->screen); p->state = VGAT_STATE_NORMAL; }
        else            { p->state = VGAT_STATE_NORMAL; }
        break;

      case VGAT_STATE_CSI:
        if (c >= '0' && c <= '9') { p->params[p->nparams] = p->params[p->nparams] * 10 + (c - '0'); }
        else if (c == ';' || c == ':') { if (p->nparams < 15) p->params[++p->nparams] = 0; }
        else if (c >= '<' && c <= '?') { p->private_marker = (char)c; }
        else if (c >= 0x20 && c <= 0x2F) { /* CSI intermediate byte */ }
        else if (c >= 0x40 && c <= 0x7E) { p->nparams++; csi_final(p, (char)c); p->state = VGAT_STATE_NORMAL; }
        else { p->state = VGAT_STATE_NORMAL; }
        break;

      case VGAT_STATE_OSC:
        if (c == 0x07)      { osc_dispatch(p); p->state = VGAT_STATE_NORMAL; }
        else if (c == 0x1B) { p->state = VGAT_STATE_OSC_ESC; }
        else if (p->osc_len < (int)sizeof(p->osc_buf) - 1) { p->osc_buf[p->osc_len++] = (char)c; }
        break;

      case VGAT_STATE_OSC_ESC:
        if (c == '\\') { osc_dispatch(p); }
        p->state = VGAT_STATE_NORMAL;
        break;
    }
  }
}
