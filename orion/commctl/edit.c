#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/theme.h>
#include "commctl.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__) && !defined(AX_PLATFORM_TOUCH)
#include <CoreFoundation/CoreFoundation.h>
#include <objc/runtime.h>
#include <objc/message.h>
#elif defined(__linux__) && !defined(__ANDROID__)
#if defined(__has_include)
#if __has_include(<X11/Xlib.h>)
#define EDIT_X11_CLIPBOARD 1
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <poll.h>
#include <time.h>
#endif
#endif
#endif

#define BUFFER_SIZE 512
#define EDIT_MASK_CAP 32

extern uint32_t ui_get_mod_state(void);

// Helper function (will be moved to ui/user/window.c later)
extern window_t *get_root_window(window_t *window);

typedef struct {
  char placeholder[128], leading_icon[64];
  uint32_t list;   // edSetList: the list this field drives (0 = none)
  bool edited;     // typed into since the text was last submitted (edUpdate)
  char *password;  // bounded private allocation; never window title or binding text
  size_t password_length;
} textedit_t;

// Inset by the vertical margin on the left too, so the icon sits centred in the
// rounded end of a capsule field.
static irect16_t textedit_icon_rect(window_t *win) {
  const char *sel = ui_selector(win, "TextEdit");
  int size = MIN(ui_px(sel, "--icon-size"), MAX(0, win->frame.h - 2 * ui_px(sel, "--icon-margin"))), inset = (win->frame.h - size) / 2;
  return R(inset, inset, size, size);
}

static int textedit_text_x(window_t *win) {
  const textedit_t *te = win->userdata;
  if (!te || !te->leading_icon[0]) return edit_padding_x(win);
  irect16_t icon = textedit_icon_rect(win);
  return icon.x + icon.w + ui_px(ui_selector(win, "TextEdit"), "--icon-gap");
}

static void notify_change(window_t *win) {
  textedit_t *te = win->userdata;
  if (te) te->edited = true;
  if (win->parent) send_message(get_root_window(win), evCommand, MAKEDWORD(win->id, ednChange), win);
}

static window_t *driven_list(window_t *win) {
  const textedit_t *te = win->userdata;
  window_t *list = te && te->list ? get_window_item(get_root_window(win), te->list) : NULL;
  return list && window_is_visible(list) ? list : NULL;
}

// A field that drives a list moves the list's selection with the arrow keys
// and opens its selected row with Enter, until something new is typed.
static bool drive_list(window_t *win, uint32_t key) {
  window_t *list = driven_list(win);
  textedit_t *te = win->userdata;
  if (!list) return false;
  if (key == AX_KEY_UPARROW || key == AX_KEY_DOWNARROW) return send_message(list, evKeyDown, key, NULL) != 0;
  if (key == AX_KEY_ENTER && !te->edited && (int)send_message(list, RVM_GETSELECTION, 0, NULL) >= 0)
    return send_message(list, evKeyDown, key, NULL) != 0;
  return false;
}

static void submit(window_t *win) {
  textedit_t *te = win->userdata;
  if (te) te->edited = false;
  send_message(get_root_window(win), evCommand, MAKEDWORD(win->id, edUpdate), win);
}

static int edit_previous(const char *text, int at) {
  if (at > 0) at--;
  while (at > 0 && ((unsigned char)text[at] & 0xc0) == 0x80) at--;
  return at;
}

static int edit_next(const char *text, int at) {
  if (text[at]) at++;
  while (text[at] && ((unsigned char)text[at] & 0xc0) == 0x80) at++;
  return at;
}

static void password_erase(void *mem, size_t cap) {
  volatile unsigned char *p = mem;
  while (cap--) *p++ = 0;
}

static void password_clear(window_t *win) {
  textedit_t *te = win->userdata;
  if (te && te->password) {
    password_erase(te->password, TEXTEDIT_PASSWORD_CAPACITY);
    free(te->password);
    te->password = NULL;
    te->password_length = 0;
    te->edited = false;
  }
  win->cursor_pos = 0;
  window_set_state(win, WINDOW_STATE_EDITING, false);
}

bool textedit_take_password(window_t *win, char *out, size_t capacity) {
  if (out && capacity) out[0] = 0;
  if (!win || win->proc != win_textedit || !(win->flags & TEXTEDIT_PASSWORD)) return false;
  const textedit_t *te = win->userdata;
  bool ok = te && te->password_length && out && capacity > te->password_length;
  if (ok) memcpy(out, te->password, te->password_length + 1);
  password_clear(win);
  invalidate_window(win);
  return ok;
}

static size_t password_length(const window_t *win) {
  const textedit_t *te = win->userdata;
  return te ? te->password_length : 0;
}

static textedit_t *password_storage(window_t *win) {
  textedit_t *te = win->userdata;
  if (!te) { te = calloc(1, sizeof(*te)); win->userdata = te; }
  if (!te) return NULL;
  if (!te->password) te->password = calloc(TEXTEDIT_PASSWORD_CAPACITY, 1);
  return te->password ? te : NULL;
}

// Tokens and keys are printable ASCII. Reject an entire invalid/overlong
// paste instead of retaining a plausible yet incorrect partial credential.
static bool password_insert(window_t *win, const char *chunk, size_t len) {
  if (!chunk || !len || len >= TEXTEDIT_PASSWORD_CAPACITY || window_is_disabled(win)) return false;
  for (size_t i = 0; i < len; ++i) {
    unsigned char b = (unsigned char)chunk[i];
    if (b < 33 || b > 126) return false;
  }
  textedit_t *te = password_storage(win);
  if (!te || len >= TEXTEDIT_PASSWORD_CAPACITY - te->password_length) return false;
  size_t at = window_has_state(win, WINDOW_STATE_EDITING) ? win->cursor_pos : te->password_length;
  if (at > te->password_length) at = te->password_length;
  memmove(te->password + at + len, te->password + at, te->password_length - at + 1);
  memcpy(te->password + at, chunk, len);
  te->password_length += len;
  win->cursor_pos = (uint32_t)(at + len);
  window_set_state(win, WINDOW_STATE_EDITING, true);
  invalidate_window(win);
  return true;
}

// Native read-only clipboard bridges. The application never publishes text
// from this control, and wipes its own clipboard copy after the paste.
static size_t password_clipboard(char out[TEXTEDIT_PASSWORD_CAPACITY]) {
#if defined(_WIN32)
  if (!OpenClipboard(NULL)) return 0;
  HANDLE handle = GetClipboardData(CF_UNICODETEXT);
  const WCHAR *wide = handle ? GlobalLock(handle) : NULL;
  size_t count = handle ? (size_t)GlobalSize(handle) / sizeof(WCHAR) : 0, len = 0;
  if (wide) {
    for (; len < count && len < TEXTEDIT_PASSWORD_CAPACITY && wide[len]; len++) {
      if (wide[len] < 33 || wide[len] > 126) break;
      out[len] = (char)wide[len];
    }
    if (len == count || len == TEXTEDIT_PASSWORD_CAPACITY || wide[len]) len = 0;
    GlobalUnlock(handle);
  }
  CloseClipboard();
  return len;
#elif defined(__APPLE__) && !defined(AX_PLATFORM_TOUCH)
  // AppKit is already linked for the desktop. Invoke its pasteboard from
  // this C compilation unit without adding an Objective-C build target.
  id cls = (id)objc_getClass("NSPasteboard");
  id board = ((id (*)(id, SEL))objc_msgSend)(cls, sel_registerName("generalPasteboard"));
  id string = ((id (*)(id, SEL, id))objc_msgSend)(board, sel_registerName("stringForType:"),
                                                (id)CFSTR("public.utf8-plain-text"));
  if (!string || CFStringGetLength((CFStringRef)string) >= TEXTEDIT_PASSWORD_CAPACITY
      || !CFStringGetCString((CFStringRef)string, out, TEXTEDIT_PASSWORD_CAPACITY, kCFStringEncodingASCII))
    return 0;
  // With ASCII encoding, bytes and CF code units agree. Unlike strlen,
  // the explicit length does not silently accept embedded NUL.
  return (size_t)CFStringGetLength((CFStringRef)string);
#elif defined(EDIT_X11_CLIPBOARD)
  // Desktop X11/XWayland. Keep a separate temporary connection so Orion's
  // normal event queue never sees clipboard payloads. Accept only a small,
  // single property on a local display; do not wait for INCR streams.
  const char *display = getenv("DISPLAY");
  if (!display || (display[0] != ':' && strncmp(display, "unix:", 5))) return 0;
  void *x11 = dlopen("libX11.so.6", RTLD_LOCAL | RTLD_NOW);
  if (!x11) return 0;
#define EDIT_X11(fn) __typeof__(&fn) p_##fn = (__typeof__(&fn))dlsym(x11, #fn); if (!p_##fn) { dlclose(x11); return 0; }
  EDIT_X11(XOpenDisplay)
  EDIT_X11(XCloseDisplay)
  EDIT_X11(XCreateSimpleWindow)
  EDIT_X11(XInternAtom)
  EDIT_X11(XConvertSelection)
  EDIT_X11(XFlush)
  EDIT_X11(XPending)
  EDIT_X11(XNextEvent)
  EDIT_X11(XGetWindowProperty)
  EDIT_X11(XFree)
#undef EDIT_X11
  Display *dpy = p_XOpenDisplay(display);
  if (!dpy) { dlclose(x11); return 0; }
  Window requestor = p_XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);
  Atom clipboard = p_XInternAtom(dpy, "CLIPBOARD", False);
  Atom utf8 = p_XInternAtom(dpy, "UTF8_STRING", False);
  Atom property = p_XInternAtom(dpy, "ORION_PASSWORD_PASTE", False);
  struct timespec start, now;
  size_t result = 0;
  if (!requestor || clipboard == None || utf8 == None || property == None || clock_gettime(CLOCK_MONOTONIC, &start))
    goto clipboard_finish;
  p_XConvertSelection(dpy, clipboard, utf8, property, requestor, CurrentTime);
  p_XFlush(dpy);
  for (;;) {
    if (clock_gettime(CLOCK_MONOTONIC, &now)) break;
    long elapsed = (now.tv_sec - start.tv_sec) * 1000L + (now.tv_nsec - start.tv_nsec) / 1000000L;
    if (elapsed >= 160) break;
    if (!p_XPending(dpy)) {
      struct pollfd fd = { .fd = ConnectionNumber(dpy), .events = POLLIN };
      if (poll(&fd, 1, (int)(160 - elapsed)) <= 0 || !(fd.revents & POLLIN)) break;
      continue;
    }
    XEvent event;
    p_XNextEvent(dpy, &event);
    if (event.type != SelectionNotify || event.xselection.requestor != requestor
        || event.xselection.selection != clipboard || event.xselection.target != utf8) continue;
    if (event.xselection.property == property) {
      unsigned char *data = NULL;
      Atom type = None;
      int format = 0;
      unsigned long count = 0, remaining = 0;
      if (p_XGetWindowProperty(dpy, requestor, property, 0, (TEXTEDIT_PASSWORD_CAPACITY + 3) / 4, True,
                               AnyPropertyType, &type, &format, &count, &remaining, &data) == Success
          && data && format == 8) {
        if (type == utf8 && !remaining && count < TEXTEDIT_PASSWORD_CAPACITY) {
          memcpy(out, data, count);
          result = (size_t)count;
        }
        password_erase(data, count);
      }
      if (data) p_XFree(data);
    }
    break;
  }
clipboard_finish:
  p_XCloseDisplay(dpy);
  dlclose(x11);
  return result;
#else
  (void)out;
  return 0;
#endif
}

bool textedit_paste_password(window_t *win) {
  if (!win || win->proc != win_textedit || !(win->flags & TEXTEDIT_PASSWORD) || window_is_disabled(win))
    return false;
  char clipboard[TEXTEDIT_PASSWORD_CAPACITY];
  size_t len = password_clipboard(clipboard);
  bool valid = len > 0 && len < sizeof(clipboard);
  for (size_t i = 0; valid && i < len; i++)
    if ((unsigned char)clipboard[i] < 33 || (unsigned char)clipboard[i] > 126) valid = false;
  textedit_t *te = valid ? password_storage(win) : NULL;
  if (te) {
    password_erase(te->password, TEXTEDIT_PASSWORD_CAPACITY);
    memcpy(te->password, clipboard, len);
    te->password_length = len;
    win->cursor_pos = (uint32_t)len;
    window_set_state(win, WINDOW_STATE_EDITING, true);
    invalidate_window(win);
  }
  password_erase(clipboard, sizeof(clipboard));
  return te != NULL;
}

// Render only a bounded mask even for multi-kilobyte tokens. Position/hit
// calculations use that same mask and never inspect the characters.
static size_t password_mask(window_t *win, int available, char out[EDIT_MASK_CAP + 1]) {
  int cell = MAX(1, text_strwidth(ui_selector(win, "TextEdit"), "*"));
  size_t visible = (size_t)MAX(1, MIN(EDIT_MASK_CAP, available / cell));
  size_t len = password_length(win), cursor = MIN((size_t)win->cursor_pos, len);
  size_t start = cursor >= visible ? cursor - visible + 1 : 0;
  if (start + visible > len && len > visible) start = len - visible;
  size_t shown = MIN(visible, len - start);
  memset(out, '*', shown);
  out[shown] = '\0';
  return start;
}

static bool password_key(window_t *win, uint32_t key) {
  textedit_t *te = win->userdata;
  if (key == AX_KEY_TAB || key == AX_KEY_ESCAPE) {
    window_set_state(win, WINDOW_STATE_EDITING, false);
    return false;
  }
  int mods = (int)ui_get_mod_state();
  if (mods & (AX_MOD_CTRL | AX_MOD_CMD)) {
    // No credential path to the clipboard via copy/cut. Control+A is a
    // deliberate clear, useful for replacing an expired token with a paste.
    if (key == AX_KEY_V) textedit_paste_password(win);
    else if (key == AX_KEY_A && !window_is_disabled(win)) {
      password_clear(win);
      invalidate_window(win);
    }
    return true;
  }
  if (key == AX_KEY_ENTER) return false;  // default dialog button
  if (!te || !te->password || window_is_disabled(win)) return true;
  size_t cursor = MIN((size_t)win->cursor_pos, te->password_length);
  size_t erase = te->password_length;
  switch (key) {
    case AX_KEY_LEFTARROW: if (cursor) cursor--; break;
    case AX_KEY_RIGHTARROW: if (cursor < te->password_length) cursor++; break;
    case AX_KEY_HOME: cursor = 0; break;
    case AX_KEY_END: cursor = te->password_length; break;
    case AX_KEY_BACKSPACE: if (cursor) erase = --cursor; break;
    case AX_KEY_DEL: erase = cursor; break;
    default: return true;
  }
  if (erase < te->password_length) {
    memmove(te->password + erase, te->password + erase + 1, te->password_length - erase - 1);
    password_erase(te->password + --te->password_length, 1);
  }
  win->cursor_pos = (uint32_t)cursor;
  window_set_state(win, WINDOW_STATE_EDITING, true);
  invalidate_window(win);
  return true;
}

// Text edit control window procedure
result_t win_textedit(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  bool secure = (win->flags & TEXTEDIT_PASSWORD) != 0;
  switch (msg) {
    case evCreate:
      if (secure) password_erase(win->title, sizeof(win->title));
      win->frame.w = MAX(win->frame.w, text_strwidth(ui_selector(win, "TextEdit"), win->title) + edit_padding_x(win) * 2);
      control_apply_predefined_height(win, "textedit");
      return true;
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (m) {
        m->desired_w = MAX(ui_px(ui_selector(win, "TextEdit"), "min-width"),
                           text_strwidth(ui_selector(win, "TextEdit"), win->title) + textedit_text_x(win) + edit_padding_x(win));
        m->desired_h = control_predefined_height(win->flags);
      }
      return true;
    }
    case evArrange:
      return control_arrange_predefined_height(win, (layout_arrange_t *)lparam);
    case evPaint: {
      irect16_t local = {0, 0, win->frame.w, win->frame.h};
      ctrl_state_t state = g_ui_runtime.focused == win ? CTRL_FOCUSED : CTRL_NORMAL;
      if (window_is_disabled(win)) state |= CTRL_DISABLED;
      theme_draw_in(win, THEME_PART_FIELD, local, state);
      int th = text_char_height(ui_selector(win, "TextEdit"));
      int text_x = textedit_text_x(win);
      int text_y = MIN(control_text_padding_y(win->flags), MAX(0, (win->frame.h - th) / 2));
      const textedit_t *te = win->userdata;
      if (te && te->leading_icon[0]) {
        irect16_t icon = textedit_icon_rect(win);
        draw_sysicon(te->leading_icon, icon.x, icon.y, icon.w, get_sys_color(state & CTRL_DISABLED ? brTextDisabled : brTextSecondary));
      }
      char mask[EDIT_MASK_CAP + 1];
      int available = MAX(0, local.w - text_x - edit_padding_x(win));
      size_t start = secure ? password_mask(win, available, mask) : 0;
      const char *shown = secure ? mask : win->title;
      if (!shown[0] && te && te->placeholder[0])
        draw_text_ellipsized(ui_selector(win, "TextEdit"), te->placeholder, text_x, text_y, MAX(0, local.w - text_x - edit_padding_x(win)), get_sys_color(brTextSecondary));
      draw_text(ui_selector(win, "TextEdit"), shown, text_x, text_y, get_sys_color(brTextNormal));
      if (g_ui_runtime.focused == win && window_has_state(win, WINDOW_STATE_EDITING)) {
        size_t cursor = secure ? MIN((size_t)win->cursor_pos, password_length(win)) - start : win->cursor_pos;
        theme_draw_in(win, THEME_PART_CARET, R(text_x + text_strnwidth(ui_selector(win, "TextEdit"), shown, cursor), text_y, 2, th),
                   CTRL_NORMAL);
      }
      return true;
    }
#ifdef AX_PLATFORM_TOUCH
    case evKillFocus:
      axSetTextInput(FALSE);
      return true;
#endif
    case evDestroy:
      if (secure) password_clear(win);
#ifdef AX_PLATFORM_TOUCH
      axSetTextInput(FALSE);
#endif
      free(win->userdata);
      win->userdata = NULL;
      return true;
    case evLeftButtonUp:
      if (g_ui_runtime.focused == win) {
        invalidate_window(win);
        window_set_state(win, WINDOW_STATE_EDITING, true);
#ifdef AX_PLATFORM_TOUCH
        axSetTextInput(TRUE);
#endif
        int text_x = textedit_text_x(win);
        if (secure) {
          char mask[EDIT_MASK_CAP + 1];
          size_t start = password_mask(win, MAX(0, win->frame.w - text_x - edit_padding_x(win)), mask);
          int cell = MAX(1, text_strwidth(ui_selector(win, "TextEdit"), "*"));
          size_t selected = (size_t)MAX(0, ((int)LOWORD(wparam) - text_x + cell / 2) / cell);
          win->cursor_pos = (uint32_t)(start + MIN(selected, strlen(mask)));
          return true;
        }
        win->cursor_pos = 0;
        for (int i = 0;; i = edit_next(win->title, i)) {
          int x1 = text_x + text_strnwidth(ui_selector(win, "TextEdit"), win->title, i);
          int x2 = text_x + text_strnwidth(ui_selector(win, "TextEdit"), win->title, win->cursor_pos);
          if (abs((int)LOWORD(wparam) - x1) < abs((int)LOWORD(wparam) - x2)) {
            win->cursor_pos = i;
          }
          if (!win->title[i]) break;
        }
      }
      return true;
    case evTextInput: {
      if (!lparam) return false;
      if (secure) {
        if (!(ui_get_mod_state() & (AX_MOD_CTRL | AX_MOD_CMD))) {
          const char *chunk = lparam;
          size_t len = 0;
          while (len < TEXTEDIT_PASSWORD_CAPACITY && chunk[len]) len++;
          password_insert(win, chunk, len);
        }
        return true;
      }
      // Typing edits, however the field got the focus (a click, Tab, or a
      // command that focuses it), so Enter submits and Backspace deletes.
      if (!window_has_state(win, WINDOW_STATE_EDITING)) {
        win->cursor_pos = (uint32_t)strlen(win->title);
        window_set_state(win, WINDOW_STATE_EDITING, true);
      }
      size_t added = strlen(lparam);
      if (strlen(win->title) + added < BUFFER_SIZE) {
        memmove(win->title + win->cursor_pos + added,
                win->title + win->cursor_pos,
                strlen(win->title + win->cursor_pos) + 1);
        memcpy(win->title + win->cursor_pos, lparam, added);
        win->cursor_pos += (int)added;
        notify_change(win);
      }
      invalidate_window(win);
      return true;
    }
    case evKeyDown:
      if (secure) return password_key(win, wparam);
      if (drive_list(win, wparam)) { invalidate_window(win); return true; }
      switch (wparam) {
        case AX_KEY_TAB:
          if (window_has_state(win, WINDOW_STATE_EDITING)) {
            submit(win);
            window_set_state(win, WINDOW_STATE_EDITING, false);
          }
          return false;
        case AX_KEY_ENTER:
          if (!window_has_state(win, WINDOW_STATE_EDITING)) {
            win->cursor_pos = (int)strlen(win->title);
            window_set_state(win, WINDOW_STATE_EDITING, true);
          } else {
            submit(win);
            window_set_state(win, WINDOW_STATE_EDITING, false);
          }
          break;
        case AX_KEY_ESCAPE:   // leaves editing; the key goes on, so a dialog cancels (as an EDIT leaves VK_ESCAPE to its dialog)
          window_set_state(win, WINDOW_STATE_EDITING, false);
          return false;
        case AX_KEY_BACKSPACE:
          if (win->cursor_pos > 0 && window_has_state(win, WINDOW_STATE_EDITING)) {
            int previous = edit_previous(win->title, win->cursor_pos);
            memmove(win->title + previous,
                    win->title + win->cursor_pos,
                    strlen(win->title + win->cursor_pos) + 1);
            win->cursor_pos = previous;
            notify_change(win);
          }
          break;
        case AX_KEY_LEFTARROW:
          if (win->cursor_pos > 0 && window_has_state(win, WINDOW_STATE_EDITING)) {
            win->cursor_pos = edit_previous(win->title, win->cursor_pos);
          }
          break;
        case AX_KEY_RIGHTARROW:
          if (win->cursor_pos < strlen(win->title) && window_has_state(win, WINDOW_STATE_EDITING)) {
            win->cursor_pos = edit_next(win->title, win->cursor_pos);
          }
          break;
        default:
          return window_has_state(win, WINDOW_STATE_EDITING);
      }
      invalidate_window(win);
      return true;

    case edGetText: {
      if (secure) {
        if (lparam && wparam) ((char *)lparam)[0] = 0;
        return 0;
      }
      if (!lparam || wparam == 0)
        return (result_t)strlen(win->title);
      size_t sz = (size_t)wparam;
      strncpy((char *)lparam, win->title, sz - 1);
      ((char *)lparam)[sz - 1] = '\0';
      return (result_t)strlen((char *)lparam);
    }

    case edSetText:
      if (secure) {
        if (lparam && *(const char *)lparam) return false;
        password_clear(win);
        invalidate_window(win);
        return true;
      }
      if (lparam) {
        strncpy(win->title, (const char *)lparam, BUFFER_SIZE - 1);
        win->title[BUFFER_SIZE - 1] = '\0';
        if ((size_t)win->cursor_pos > strlen(win->title))
          win->cursor_pos = (uint32_t)strlen(win->title);
        invalidate_window(win);
      }
      return true;

    case edSetLeadingIcon:
    case edSetPlaceholder: {
      textedit_t *te = win->userdata ? win->userdata : allocate_window_data(win, sizeof(textedit_t));
      if (!te) {
        fprintf(stderr, "[ed] set decoration failed win=%u msg=%u reason=allocation\n", (unsigned)win->id, msg);
        fflush(stderr);
        return false;
      }
      if (msg == edSetLeadingIcon) snprintf(te->leading_icon, sizeof(te->leading_icon), "%s", lparam ? (const char *)lparam : "");
      else snprintf(te->placeholder, sizeof(te->placeholder), "%s", lparam ? (const char *)lparam : "");
      invalidate_window(win);
      return true;
    }

    case edSetList: {
      if (secure) return false;
      textedit_t *te = win->userdata ? win->userdata : allocate_window_data(win, sizeof(textedit_t));
      if (!te) {
        fprintf(stderr, "[ed] edSetList failed win=%u list=%u reason=allocation\n", (unsigned)win->id, (unsigned)wparam);
        fflush(stderr);
        return false;
      }
      te->list = wparam;
      return true;
    }
    case edGetList: {
      const textedit_t *te = win->userdata;
      return te ? (result_t)te->list : 0;
    }

    case edGetPlaceholder: {
      textedit_t *te = win->userdata;
      return (result_t)(intptr_t)(te ? te->placeholder : "");
    }

    case evGetCursor:
      return curIBeam;
  }
  return false;
}
