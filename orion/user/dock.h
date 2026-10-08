#ifndef __DOCK_H__
#define __DOCK_H__

#include "user.h"

typedef enum { DOCK_FLOAT, DOCK_TOP, DOCK_LEFT, DOCK_BOTTOM, DOCK_RIGHT, DOCK_FILL } dock_side_t;
#define DOCK_EDGE(side) (1u << (side))
#define DOCK_ALL_EDGES (DOCK_EDGE(DOCK_TOP) | DOCK_EDGE(DOCK_LEFT) | DOCK_EDGE(DOCK_BOTTOM) | DOCK_EDGE(DOCK_RIGHT))
#define DOCK_RESIZABLE 1u
#define DOCK_TOOLBAR   2u
#define DOCK_MENU      4u
#define DOCK_NOFLOAT   8u
#define DOCK_SPLITTER  5
#define evDockChanged (evUser + 910)
#define evDockOrient  (evUser + 911) // wparam: true for a vertical menu
#define evDockMeasure (evUser + 912) // lparam: ipoint16_t preferred menu size

// The host owns its panes in every state, including floating. Extents are logical pixels.
typedef struct dock_state_s {
  dock_side_t side, last_side;
  uint32_t allowed, flags;
  int extent, minimum, order;
  bool collapsed;
  irect16_t floating, splitter;
} dock_state_t;

// Per-host layout bookkeeping, allocated on the first dock_layout().
typedef struct dock_host_s {
  bool busy;           // re-entrancy guard while panes are being placed
  irect16_t content;   // area left over for non-docked content
} dock_host_t;

bool dock_window(window_t *win, dock_side_t side, uint32_t allowed, uint32_t flags, int extent, int minimum);
bool dock_set_side(window_t *win, dock_side_t side);
bool dock_float(window_t *win, irect16_t frame);
bool dock_collapse(window_t *win, bool collapsed);
irect16_t dock_layout(window_t *host, irect16_t area);
irect16_t dock_content_rect(window_t *host);
bool dock_handle_event(ui_event_t *event);
void dock_cancel_window(window_t *win);
void dock_forget_window(window_t *win);
void dock_paint(window_t *host);
void dock_paint_overlay(window_t *host);
int dock_cursor(window_t *hit, ipoint16_t screen);
window_t *dock_hit_test(window_t *host, ipoint16_t point);
bool dock_is_floating(const window_t *win);

#endif
