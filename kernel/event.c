// SDL event handling and dispatch
// Extracted from mapview/window.c

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "../user/user.h"
#include "../user/messages.h"

// External references
extern bool running;
extern window_t *windows;
extern window_t *_focused;
extern window_t *_tracked;
extern window_t *_captured;

// Macros for coordinate conversion
#define SCALE_POINT(x) ((x)/UI_WINDOW_SCALE)
#define LOCAL_X(VALUE, WIN) (SCALE_POINT((VALUE).x) - (WIN)->frame.x + (WIN)->scroll[0])
#define LOCAL_Y(VALUE, WIN) (SCALE_POINT((VALUE).y) - (WIN)->frame.y + (WIN)->scroll[1])
#define CONTAINS(x, y, x1, y1, w1, h1) \
((x1) <= (x) && (y1) <= (y) && (x1) + (w1) > (x) && (y1) + (h1) > (y))

// External functions
extern int send_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern void move_window(window_t *win, int x, int y);
extern void resize_window(window_t *win, int new_w, int new_h);
extern window_t *find_window(int x, int y);
extern void set_focus(window_t* win);
extern void track_mouse(window_t *win);
extern void show_window(window_t *win, bool visible);
extern void end_dialog(window_t *win, uint32_t code);

// Drag/resize state (shared with user/window.c for destroy_window cleanup)
window_t *_dragging = NULL;
window_t *_resizing = NULL;
static int drag_anchor[2];

// Handle mouse events on child windows
static int handle_mouse(int msg, window_t *win, int x, int y) {
  for (window_t *c = win->children; c; c = c->next) {
    if (CONTAINS(x, y, c->frame.x, c->frame.y, c->frame.w, c->frame.h) &&
        c->proc(c, msg, MAKEDWORD(x, y), NULL))
    {
      return true;
    }
  }
  return false;
}

// Find next tab stop
window_t* find_next_tab_stop(window_t *win, bool allow_current) {
  if (!win) return false;
  window_t *next;
  if ((next = find_next_tab_stop(win->children, true))) return next;
  if (!win->notabstop && (win->parent || win->visible) && allow_current) return win;
  if ((next = find_next_tab_stop(win->next, true))) return next;
  return allow_current ? NULL : find_next_tab_stop(win->parent, false);
}

// Find previous tab stop
window_t* find_prev_tab_stop(window_t* win) {
  window_t *it = (win = (win->parent ? win : find_next_tab_stop(win, false)));
  for (window_t *next = find_next_tab_stop(it, false); next != win;
       it = next, next = find_next_tab_stop(next, false));
  return it;
}

// Move window to top of Z-order
void move_to_top(window_t* _win) {
  extern window_t *get_root_window(window_t *window);
  extern void post_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
  extern void invalidate_window(window_t *win);
  
  window_t *win = get_root_window(_win);
  post_message(win, kWindowMessageRefreshStencil, 0, NULL);
  invalidate_window(win);
  
  if (win->flags&WINDOW_ALWAYSINBACK)
    return;
  
  window_t **head = &windows, *p = NULL, *n = *head;
  
  // Find the node `win` in the list
  while (n != win) {
    p = n;
    n = n->next;
    if (!n) return;  // If `win` is not found, exit
  }
  
  // Remove the node `win` from the list
  if (p) p->next = win->next;
  else *head = win->next;  // If `win` is at the head, update the head
  
  // If `win` was the only node, just re-add it and return
  if (!*head) {
    *head = win;
    win->next = NULL;
    return;
  }

  if (win->flags & WINDOW_ALWAYSONTOP) {
    // ALWAYSONTOP windows always go to the absolute end of the list so they
    // are rendered last (on top) and receive mouse clicks preferentially.
    window_t *tail = *head;
    while (tail->next)
      tail = tail->next;
    tail->next = win;
    win->next = NULL;
  } else {
    // Regular windows are inserted just before the first ALWAYSONTOP window
    // so they never obscure the always-on-top palette / menu-bar windows.
    window_t *prev = NULL, *cur = *head;
    while (cur && !(cur->flags & WINDOW_ALWAYSONTOP)) {
      prev = cur;
      cur  = cur->next;
    }
    // cur == first ALWAYSONTOP window (or NULL if none)
    win->next = cur;
    if (prev) prev->next = win;
    else      *head      = win;
  }
}

// Dispatch SDL event to window system
void dispatch_message(SDL_Event *evt) {
  window_t *win;
  switch (evt->type) {
    case SDL_QUIT:
      running = false;
      break;
    case SDL_TEXTINPUT:
      send_message(_focused, kWindowMessageTextInput, 0, evt->text.text);
      break;
    case SDL_KEYDOWN:
      if (_focused && !send_message(_focused, kWindowMessageKeyDown, evt->key.keysym.scancode, NULL)) {
        switch (evt->key.keysym.scancode) {
          case SDL_SCANCODE_TAB:
            if (evt->key.keysym.mod & KMOD_SHIFT) {
              set_focus(find_prev_tab_stop(_focused));
            } else {
              set_focus(find_next_tab_stop(_focused, false));
            }
            break;
          default:
            break;
        }
      }
      break;
    case SDL_KEYUP:
      send_message(_focused, kWindowMessageKeyUp, evt->key.keysym.scancode, NULL);
      break;
    case SDL_JOYAXISMOTION:
      send_message(_focused, kWindowMessageJoyAxisMotion, MAKEDWORD(evt->jaxis.axis, evt->jaxis.value), NULL);
      break;
    case SDL_JOYBUTTONDOWN:
      send_message(_focused, kWindowMessageJoyButtonDown, evt->jbutton.button, NULL);
      break;
    case SDL_MOUSEMOTION:
      if (_dragging) {
        move_window(_dragging,
                    SCALE_POINT(evt->motion.x) - drag_anchor[0],
                    SCALE_POINT(evt->motion.y) - drag_anchor[1]);
      } else if (_resizing) {
        int new_w = SCALE_POINT(evt->motion.x) - _resizing->frame.x;
        int new_h = SCALE_POINT(evt->motion.y) - _resizing->frame.y;
        resize_window(_resizing, new_w, new_h);
      } else if (((win = _captured) ||
                  (win = find_window(SCALE_POINT(evt->motion.x),
                                     SCALE_POINT(evt->motion.y)))))
      {
        if (win->disabled) return;
        int16_t x = LOCAL_X(evt->motion, win);
        int16_t y = LOCAL_Y(evt->motion, win);
        int16_t dx = evt->motion.xrel;
        int16_t dy = evt->motion.yrel;
        if (y >= 0 && (win == _captured || win == _focused)) {
          send_message(win, kWindowMessageMouseMove, MAKEDWORD(x, y), (void*)(intptr_t)MAKEDWORD(dx, dy));
        }
      }
      if (_tracked && !CONTAINS(SCALE_POINT(evt->motion.x),
                                SCALE_POINT(evt->motion.y),
                                _tracked->frame.x, _tracked->frame.y,
                                _tracked->frame.w, _tracked->frame.h))
      {
        track_mouse(NULL);
      }
      break;
    case SDL_MOUSEWHEEL:
      if ((win = _captured) ||
          (win = find_window(SCALE_POINT(evt->wheel.mouseX),
                             SCALE_POINT(evt->wheel.mouseY))))
      {
        if (win->disabled) return;
        send_message(win, kWindowMessageWheel, MAKEDWORD(-evt->wheel.x * SCROLL_SENSITIVITY, evt->wheel.y * SCROLL_SENSITIVITY), NULL);
      }
      break;
    case SDL_MOUSEBUTTONDOWN:
      if ((win = _captured) ||
          (win = find_window(SCALE_POINT(evt->button.x),
                             SCALE_POINT(evt->button.y))))
      {
        if (win->disabled) return;
        bool activating = (win != _focused);
        window_t *old_root = _focused ? get_root_window(_focused) : NULL;
        window_t *new_root = get_root_window(win);
        bool root_changing = activating && (new_root != old_root);
        if (activating) {
          send_message(win, kWindowMessageMouseActivate, 0, NULL);
          if (root_changing && old_root)
            send_message(old_root, kWindowMessageActivate, WA_INACTIVE, new_root);
        }
        if (!win->parent) {
          move_to_top(win);
        }
        if (activating) {
          set_focus(win);
          if (root_changing)
            send_message(new_root, kWindowMessageActivate, WA_CLICKACTIVE, old_root);
        }
        int x = LOCAL_X(evt->button, win);
        int y = LOCAL_Y(evt->button, win);
        if (x >= win->frame.w - RESIZE_HANDLE &&
            y >= win->frame.h - RESIZE_HANDLE &&
            !win->parent &&
            !(win->flags&WINDOW_NORESIZE) &&
            win != _captured)
        {
          _resizing = win;
        } else if (SCALE_POINT(evt->button.y) < win->frame.y && !win->parent && win != _captured) {
          _dragging = win;
          drag_anchor[0] = SCALE_POINT(evt->button.x) - win->frame.x;
          drag_anchor[1] = SCALE_POINT(evt->button.y) - win->frame.y;
        } else {
          int msg = 0;
          switch (evt->button.button) {
            case 1: msg = kWindowMessageLeftButtonDown; break;
            case 3: msg = kWindowMessageRightButtonDown; break;
          }
          if (!handle_mouse(msg, win, x, y)) {
            send_message(win, msg, MAKEDWORD(x, y), NULL);
          }
        }
      }
      break;
      
    case SDL_MOUSEBUTTONUP:
      if (_dragging) {
        int x = SCALE_POINT(evt->button.x);
        int y = SCALE_POINT(evt->button.y);
        int b = (_dragging->frame.x + _dragging->frame.w - CONTROL_BUTTON_PADDING - x) / CONTROL_BUTTON_WIDTH;
        if (b == 0) {
          if (_dragging->flags & WINDOW_DIALOG) {
            end_dialog(_dragging, -1);
          } else {
            show_window(_dragging, false);
          }
          _dragging = NULL;
        } else {
          switch (evt->button.button) {
            case 1: send_message(_dragging, kWindowMessageNonClientLeftButtonUp, MAKEDWORD(x, y), NULL); break;
              // case 3: send_message(win, kWindowMessageNonClientRightButtonDown, MAKEDWORD(x, y), NULL); break;
          }
          _dragging = NULL;
        }
      } else if (_resizing) {
        _resizing = NULL;
      } else if ((win = _captured) ||
                 (win = find_window(SCALE_POINT(evt->button.x),
                                    SCALE_POINT(evt->button.y))))
      {
        if (win->disabled) return;
        if (SCALE_POINT(evt->button.y) >= win->frame.y || win == _captured) {
          int x = LOCAL_X(evt->button, win);
          int y = LOCAL_Y(evt->button, win);
          int msg = 0;
          switch (evt->button.button) {
            case 1: msg = kWindowMessageLeftButtonUp; break;
            case 3: msg = kWindowMessageRightButtonUp; break;
          }
          if (!handle_mouse(msg, win, x, y)) {
            send_message(win, msg, MAKEDWORD(x, y), NULL);
          }
        } else {
          int x = SCALE_POINT(evt->button.x);
          int y = SCALE_POINT(evt->button.y);
          switch (evt->button.button) {
            case 1: send_message(win, kWindowMessageNonClientLeftButtonUp, MAKEDWORD(x, y), NULL); break;
              //              case 3: send_message(win, kWindowMessageNonClientRightButtonDown, MAKEDWORD(x, y), NULL); break;
          }
        }
      }
      break;
  }
}

// Get next SDL event
int get_message(SDL_Event *evt) {
  return SDL_PollEvent(evt);
}
