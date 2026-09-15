#ifndef __UI_TOOLBAR_H__
#define __UI_TOOLBAR_H__

#include <stdbool.h>
#include <stdint.h>

#include "user.h"

typedef enum { TOOLBAR_DOCK_TOP = 1, TOOLBAR_DOCK_LEFT = 2, TOOLBAR_DOCK_MENU = 3 } toolbar_dock_t;
typedef enum { TOOLBAR_PRESENTATION_NORMAL, TOOLBAR_PRESENTATION_COMPACT } toolbar_presentation_t;
typedef struct {
  const toolbar_item_t *items;
  int count;
  toolbar_presentation_t presentation;
} application_toolbar_t;

// Docked toolbars are owned children; use the remaining rectangle for content.
window_t *create_docked_toolbar(window_t *owner, toolbar_dock_t dock, winproc_t proc);
irect16_t layout_docked_toolbars(window_t *owner, irect16_t area);

toolbar_state_t *toolbar_ensure_state(window_t *win);
toolbar_state_t *toolbar_get_state(window_t *win);
int toolbar_effective_bsz(window_t const *win);
int toolbar_effective_item_height(window_t const *win);
int toolbar_effective_padding(window_t const *win);

void toolbar_draw_non_client(window_t *win);

bool toolbar_handle_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

bool toolbar_handle_notitle_nc_left_button_up(window_t *win, uint32_t wparam);

bool toolbar_dispatch_embedded_mouse(window_t *parent, uint32_t msg, int tb_x, int tb_y);

int toolbar_item_hit(const toolbar_state_t *tb, int tx, int ty);

#endif
