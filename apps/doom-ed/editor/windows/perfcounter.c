#include <mapview/map.h>
#include <mapview/gamefont.h>

// Add these to your console.c file at the top with other static variables
static struct {
  uint32_t ticks[64];
  uint32_t last_fps_update;   // Last time the FPS was calculated
  uint32_t frame_count;       // Number of frames since last update
  float current_fps;        // Current FPS value to display
  char fps_text[32];        // Buffer for the FPS text
  uint32_t counter;
} fps_state = {0};

result_t win_perf(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evPaint: {
      uint32_t ticks = (uint32_t)axGetMilliseconds();
      fps_state.ticks[fps_state.counter++&63] = ticks - fps_state.last_fps_update;
      fps_state.last_fps_update = ticks;
      
      uint32_t totals = 0;
      for (int i = 0; i < 64; i++) {
        totals += fps_state.ticks[i];
      }
      
      fps_state.current_fps = 64 * 1000.f / totals;
      
      // Format the FPS text
      snprintf(fps_state.fps_text, sizeof(fps_state.fps_text), "FPS: %.1f", fps_state.current_fps);
      
      extern int sectors_drawn;
      char sec[64]={0};
      snprintf(sec, sizeof(sec), "SECTORS: %d", sectors_drawn);
      
      int x = CONSOLE_PADDING;
      int y = CONSOLE_PADDING;
      
      // Draw the FPS text
      draw_text_gl3(fps_state.fps_text, x, y, 1.0f);
      draw_text_gl3(sec, x, y+LINE_HEIGHT, 1.0f);
      return true;
    }
  }
  return false;
}
