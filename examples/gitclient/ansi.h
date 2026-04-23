// ANSI SGR (Select Graphic Rendition) color and style parsing.
// Converts ANSI escape sequences to RGBA colors and 16-color palette indices.

#ifndef __GITCLIENT_ANSI_H__
#define __GITCLIENT_ANSI_H__

#include <stdint.h>
#include <stdbool.h>

// ANSI 16-color palette (indices 0..15)
// Order: black, red, green, yellow, blue, magenta, cyan, white,
//        bright-black, bright-red, bright-green, bright-yellow,
//        bright-blue, bright-magenta, bright-cyan, bright-white
extern const uint32_t kAnsi16[16];

// Convert xterm 256-color index to RGBA (0xAARRGGBB)
uint32_t ansi256_to_rgba(int idx);

// Find nearest 16-color palette index for an RGBA color
int nearest_ansi_index(uint32_t rgba);

// Clamp color index to valid 16-color range [0..15]
int clamp_ansi_index(int idx);

// Apply single SGR code to foreground, background, and bold state
void ansi_apply_sgr(int code,
                    int *fg_idx, int *bg_idx,
                    int def_fg, int def_bg,
                    bool *bold);

// Apply multiple SGR codes in sequence, handling extended color modes
// Supports: 30-37/90-97 (fg), 40-47/100-107 (bg), 1/22 (bold),
//           38;5;n (256-color fg), 48;5;n (256-color bg),
//           38;2;r;g;b (truecolor fg), 48;2;r;g;b (truecolor bg)
void ansi_apply_sgr_codes(const int *codes, int n,
                          int *fg_idx, int *bg_idx,
                          int def_fg, int def_bg,
                          bool *bold);

#endif // __GITCLIENT_ANSI_H__
