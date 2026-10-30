#ifndef __GAMEFONT_H__
#define __GAMEFONT_H__

#include <stdbool.h>

// DOOM/Hexen font rendering (game fonts from WAD files)
void draw_text_gl3(const char* text, int x, int y, float alpha);
int get_text_width(const char* text);
bool load_console_font(void);

// Initialize and cleanup game font system
void init_gamefont(void);
void shutdown_gamefont(void);

#endif // __GAMEFONT_H__