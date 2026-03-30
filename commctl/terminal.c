#include <SDL2/SDL.h>
#include "../user/gl_compat.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "commctl.h"
#include "../user/text.h"
#include "../user/user.h"
#include "../user/messages.h"

/* Platform-specific includes */
#if defined(_WIN32) || defined(_WIN64)
  #include <direct.h>  // for _chdir
  #define chdir _chdir
#else
  #include <unistd.h>  // for chdir
#endif

/* Lua headers - probe multiple locations for portability.
 * Requires -DHAVE_LUA in CFLAGS (set by the Makefile when Lua is detected).
 * Without -DHAVE_LUA the terminal compiles in command-only mode: built-in
 * commands (help, clear, exit) work normally; Lua scripting is unavailable. */
#if defined(HAVE_LUA)
#  if defined(_WIN32) || defined(_WIN64)
#    include <lua.h>
#    include <lauxlib.h>
#    include <lualib.h>
#  elif __has_include(<lua5.4/lua.h>)
#    include <lua5.4/lua.h>
#    include <lua5.4/lauxlib.h>
#    include <lua5.4/lualib.h>
#  elif __has_include(<lua.h>)
#    include <lua.h>
#    include <lauxlib.h>
#    include <lualib.h>
#  else
    /* -DHAVE_LUA was passed but the headers can't be found; disable gracefully. */
#    undef HAVE_LUA
#  endif
#endif

#define DEFAULT_TEXT_BUFFER_SIZE 4096
#if defined(HAVE_LUA)
#define TEXTBUF(L) ((text_buffer_t**)lua_getextraspace(L))
#endif

#define ICON_CURSOR 8

typedef struct text_buffer_s {
  size_t size;
  size_t capacity;
  char data[];
} text_buffer_t;

// Forward declarations
typedef struct terminal_state_s terminal_state_t;
typedef void (*terminal_cmd_func_t)(terminal_state_t *);

// Terminal command structure
typedef struct {
  const char *name;
  const char *help;
  terminal_cmd_func_t callback;
} terminal_cmd_t;

typedef struct terminal_state_s {
#if defined(HAVE_LUA)
  lua_State *L;          // Main Lua state (NULL if in command mode)
  lua_State *co;         // Coroutine for script execution (NULL if in command mode)
#else
  void *L;               // Lua not compiled in
  void *co;
#endif
  text_buffer_t *textbuf;
  char input_buffer[256];
  bool waiting_for_input;
  bool process_finished;
  bool command_mode;     // True if running in command mode (no Lua script)
} terminal_state_t;

extern void draw_icon8(int icon, int x, int y, uint32_t col);

// Forward declaration of utility function
static void f_strcat(text_buffer_t **b, const char *s);

#if defined(HAVE_LUA)
// Lua C API functions - kept minimal and grouped at the top
static int f_print(lua_State *L) {
  for (int i = 1, n = lua_gettop(L); i <= n; i++) {
    f_strcat(TEXTBUF(L), lua_tostring(L, i));
    if (i < n) f_strcat(TEXTBUF(L), "\t");
  }
  f_strcat(TEXTBUF(L), "\n");
  return 0;
}

static int f_io_read(lua_State *L) { return lua_yield(L, 0); }

static int f_io_write(lua_State *L) {
  for (int i = 1, n = lua_gettop(L); i <= n; i++) {
    const char *s = luaL_checkstring(L, i);
    f_strcat(TEXTBUF(L), s);
    fprintf(stdout, "%s", s);
  }
  return 0;
}

static int f_stdout_write(lua_State *L) {
  for (int i = 2, n = lua_gettop(L); i <= n; i++) {
    const char *s = luaL_checkstring(L, i);
    f_strcat(TEXTBUF(L), s);
    fprintf(stdout, "%s", s);
  }
  lua_pushvalue(L, 1);
  return 1;
}

static int f_stdout_flush(lua_State *L) { lua_pushvalue(L, 1); return 1; }
static int f_stdout_setvbuf(lua_State *L) { lua_pushvalue(L, 1); return 1; }

// Lua helper functions
static const char *STDOUT_METATABLE = "terminal.stdout";

const char *luaX_getpackagepath(lua_State *L) {
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "path");
  const char *path = lua_tostring(L, -1);
  lua_pop(L, 2);
  return path ? path : "";
}

const char *luaX_addcurrentfolder(lua_State *L, const char *filepath, char *filename_buf, size_t buf_size) {
  char dir[512];
  strncpy(dir, filepath, sizeof(dir));
  char *last_slash = strrchr(dir, '/');
#ifdef _WIN32
  char *last_backslash = strrchr(dir, '\\');
  if (last_backslash && (!last_slash || last_backslash > last_slash)) last_slash = last_backslash;
#endif
  
  const char *filename;
  if (last_slash) {
    *last_slash = '\0';
    filename = last_slash + 1;
  } else {
    strcpy(dir, ".");
    filename = filepath;
  }
  
  snprintf(filename_buf, buf_size, "%s", filename);
  chdir(dir);
  
  char new_path[4096];
  snprintf(new_path, sizeof(new_path), "%s;%s/?.lua", luaX_getpackagepath(L), dir);
  lua_getglobal(L, "package");
  lua_pushstring(L, new_path);
  lua_setfield(L, -2, "path");
  lua_pop(L, 1);
  
  return filename_buf;
}
#endif /* HAVE_LUA */

// Text buffer utility functions
static void init_text_buffer(text_buffer_t **buf) {
  *buf = malloc(sizeof(text_buffer_t) + DEFAULT_TEXT_BUFFER_SIZE);
  if (!*buf) return;
  (*buf)->size = 0;
  (*buf)->capacity = DEFAULT_TEXT_BUFFER_SIZE;
  (*buf)->data[0] = '\0';
}

static void free_text_buffer(text_buffer_t **buf) {
  free(*buf);
  *buf = NULL;
}

static void f_strcat(text_buffer_t **b, const char *s) {
  if (!*b || !s) return;
  
  size_t l = strlen(s);
  if ((*b)->size + l + 1 > (*b)->capacity) {
    size_t c = (*b)->capacity;
    while (c < (*b)->size + l + 1) c <<= 1;
    text_buffer_t *new_buf = realloc(*b, sizeof(text_buffer_t) + c);
    if (!new_buf) return;
    *b = new_buf;
    (*b)->capacity = c;
  }
  strcpy((*b)->data + (*b)->size, s);
  (*b)->size += l;
}

// Lua state initialization and coroutine management (only compiled with Lua)
#if defined(HAVE_LUA)
static lua_State *create_lua_state(text_buffer_t **textbuf) {
  lua_State *L = luaL_newstate();
  if (!L) return NULL;
  luaL_openlibs(L);
  
  init_text_buffer(textbuf);
  if (!*textbuf) {
    lua_close(L);
    return NULL;
  }
  
  *(text_buffer_t **)lua_getextraspace(L) = *textbuf;
  
  lua_pushcfunction(L, f_print);
  lua_setglobal(L, "print");
  
  luaL_newmetatable(L, STDOUT_METATABLE);
  lua_pushvalue(L, -1);                   lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, f_stdout_write);   lua_setfield(L, -2, "write");
  lua_pushcfunction(L, f_stdout_flush);   lua_setfield(L, -2, "flush");
  lua_pushcfunction(L, f_stdout_setvbuf); lua_setfield(L, -2, "setvbuf");
  lua_pop(L, 1);
  
  lua_newuserdata(L, sizeof(void*));
  luaL_setmetatable(L, STDOUT_METATABLE);
  
  lua_getglobal(L, "io");
  lua_pushvalue(L, -2);             lua_setfield(L, -2, "output");
  lua_pushvalue(L, -2);             lua_setfield(L, -2, "stdout");
  lua_pushcfunction(L, f_io_write); lua_setfield(L, -2, "write");
  lua_pushcfunction(L, f_io_read);  lua_setfield(L, -2, "read");
  lua_pop(L, 2);
  
  return L;
}

// Coroutine management
static void continue_coroutine(terminal_state_t *s, int nargs) {
  int nres, status = lua_resume(s->co, NULL, nargs, &nres);
  
  if (status == LUA_OK) {
    f_strcat(&s->textbuf, "\nProcess finished\n");
    s->waiting_for_input = false;
    s->process_finished = true;
  } else if (status == LUA_YIELD) {
    s->waiting_for_input = true;
    f_strcat(&s->textbuf, "\n> ");
  } else {
    f_strcat(&s->textbuf, "Error: ");
    f_strcat(&s->textbuf, lua_tostring(s->co, -1));
    f_strcat(&s->textbuf, "\n");
    s->waiting_for_input = false;
    s->process_finished = true;
  }
}
#endif /* HAVE_LUA */

// Command mode functions
static void cmd_exit(terminal_state_t *s) {
  f_strcat(&s->textbuf, "Exiting terminal...\n");
  s->process_finished = true;
  s->waiting_for_input = false;
}

// Forward declaration
static const terminal_cmd_t terminal_cmds[];

static void cmd_help(terminal_state_t *s) {
  f_strcat(&s->textbuf, "Available commands:\n");
  for (int i = 0; terminal_cmds[i].name != NULL; i++) {
    f_strcat(&s->textbuf, "  ");
    f_strcat(&s->textbuf, terminal_cmds[i].name);
    f_strcat(&s->textbuf, " - ");
    f_strcat(&s->textbuf, terminal_cmds[i].help);
    f_strcat(&s->textbuf, "\n");
  }
}

static void cmd_clear(terminal_state_t *s) {
  if (s->textbuf) {
    s->textbuf->size = 0;
    s->textbuf->data[0] = '\0';
  }
  f_strcat(&s->textbuf, "Terminal> ");
}

// Static array of available commands
static const terminal_cmd_t terminal_cmds[] = {
  {"exit", "Closes current terminal instance", cmd_exit},
  {"help", "Lists available commands", cmd_help},
  {"clear", "Clears the terminal screen", cmd_clear},
  {NULL, NULL, NULL}  // Sentinel
};

static void process_command(terminal_state_t *s, const char *cmd) {
  if (!cmd || !s) return;
  
  while (*cmd == ' ' || *cmd == '\t') cmd++;
  
  if (strlen(cmd) == 0) {
    f_strcat(&s->textbuf, "Terminal> ");
    return;
  }
  
  bool found = false;
  for (int i = 0; terminal_cmds[i].name != NULL; i++) {
    if (strcmp(cmd, terminal_cmds[i].name) == 0) {
      terminal_cmds[i].callback(s);
      found = true;
      break;
    }
  }
  
  if (!found) {
    f_strcat(&s->textbuf, "Unknown command: ");
    f_strcat(&s->textbuf, cmd);
    f_strcat(&s->textbuf, "\nType 'help' for a list of commands.\n");
  }
  
  if (!s->process_finished) {
    f_strcat(&s->textbuf, "Terminal> ");
  }
}

// Public API: Get terminal buffer content
// This function allows external code (including tests) to retrieve the current
// terminal output buffer. It safely handles null pointers and invalid window types.
const char* terminal_get_buffer(window_t *win) {
  if (!win || !win->userdata) return "";
  if (win->proc != win_terminal) return "";
  
  terminal_state_t *s = (terminal_state_t *)win->userdata;
  if (!s || !s->textbuf) return "";
  
  return s->textbuf->data;
}

result_t win_terminal(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  terminal_state_t *s = (terminal_state_t *)win->userdata;
  
  switch (msg) {
    case kWindowMessageCreate: {
      s = allocate_window_data(win, sizeof(terminal_state_t));
      if (!s) return false;
      
      win->flags |= WINDOW_VSCROLL;
      
      if (lparam == NULL) { // Command mode
        s->L = NULL;
        s->co = NULL;
        s->command_mode = true;
        s->waiting_for_input = true;
        s->process_finished = false;
        s->input_buffer[0] = '\0';
        
        init_text_buffer(&s->textbuf);
        if (!s->textbuf) return false;
        
        f_strcat(&s->textbuf, "Terminal - Command Mode\n");
        f_strcat(&s->textbuf, "Type 'help' for available commands\n");
        f_strcat(&s->textbuf, "Terminal> ");
      } else { // Script mode
#if defined(HAVE_LUA)
        s->command_mode = false;
        s->L = create_lua_state(&s->textbuf);
        if (!s->L) return false;
        s->co = lua_newthread(s->L);
        s->waiting_for_input = false;
        s->process_finished = false;
        s->input_buffer[0] = '\0';

        char filename[256];
        const char *script_file = luaX_addcurrentfolder(s->co, lparam, filename, sizeof(filename));
        
        if (luaL_loadfile(s->co, script_file) != LUA_OK) {
          f_strcat(&s->textbuf, "Error loading file: ");
          f_strcat(&s->textbuf, lua_tostring(s->co, -1));
          f_strcat(&s->textbuf, "\n");
          s->process_finished = true;
          return true;
        }
        
        continue_coroutine(s, 0);
#else
        /* Lua not compiled in: fall back to command mode with a notice. */
        s->L = NULL;
        s->co = NULL;
        s->command_mode = true;
        s->waiting_for_input = true;
        s->process_finished = false;
        s->input_buffer[0] = '\0';

        init_text_buffer(&s->textbuf);
        if (!s->textbuf) return false;

        f_strcat(&s->textbuf, "Lua scripting is not available in this build.\n");
        f_strcat(&s->textbuf, "Install Lua 5.4 (detectable by pkg-config or Homebrew) and rebuild.\n");
        f_strcat(&s->textbuf, "Terminal> ");
#endif
      }
      
      return true;
    }
    case kWindowMessageKeyDown:
      if (s->process_finished || !s->waiting_for_input) {
        return false;
      } else if (wparam == SDL_SCANCODE_RETURN) {
        f_strcat(&s->textbuf, s->input_buffer);
        f_strcat(&s->textbuf, "\n");
        
        if (s->command_mode) {
          process_command(s, s->input_buffer);
        } else if (s->co) {
#if defined(HAVE_LUA)
          lua_pushstring(s->co, s->input_buffer);
          continue_coroutine(s, 1);
#endif
        }
        
        s->input_buffer[0] = '\0';
        invalidate_window(win);
        return true;
      } else if (wparam == SDL_SCANCODE_BACKSPACE) {
        if (strlen(s->input_buffer)) {
          s->input_buffer[strlen(s->input_buffer) - 1] = '\0';
          invalidate_window(win);
        }
        return true;
      } else {
        return false;
      }
    case kWindowMessageTextInput:
      if (s->process_finished || !s->waiting_for_input) {
        return false;
      } else if (isprint(*(char*)lparam)) {
        if (strlen(s->input_buffer) < sizeof(s->input_buffer) - 1) {
          strcat(s->input_buffer, (char[]){*(char*)lparam,0});
        }
        invalidate_window(win);
        return true;
      } else {
        return false;
      }
    
    case kWindowMessageDestroy:
      if (s) {
        free_text_buffer(&s->textbuf);
#if defined(HAVE_LUA)
        if (s->L) lua_close(s->L);
#endif
        free(s);
        win->userdata = NULL;
      }
      return true;
      
    case kWindowMessagePaint: {
      if (!s) return false;
      
      rect_t viewport = {
        WINDOW_PADDING, 
        WINDOW_PADDING,
        win->frame.w - WINDOW_PADDING * 2,
        win->frame.h - WINDOW_PADDING * 2
      };
      draw_text_wrapped(s->textbuf->data, &viewport, COLOR_TEXT_NORMAL);
      
      if (s->waiting_for_input && !s->process_finished) {
        int y = win->frame.h - WINDOW_PADDING - CHAR_HEIGHT + win->scroll[1];
        draw_text_small(s->input_buffer, WINDOW_PADDING, y, COLOR_TEXT_NORMAL);
        draw_icon8(ICON_CURSOR, WINDOW_PADDING + strwidth(s->input_buffer), y, COLOR_TEXT_NORMAL);
      }
      
      return true;
    }
    
    default:
      return false;
  }
}
