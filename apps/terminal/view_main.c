// Terminal — Quake-style command console.
// Reads keyboard input and dispatches commands from a built-in table.

#include "vgat.h"

// An xterm palette index as a drawing colour (0xAABBGGRR).
static uint32_t terminal_color(int index) {
  uint32_t c = kAnsi256[index & 0xFF];
  return (c & 0xFF00FF00u) | ((c >> 16) & 0xFFu) | ((c & 0xFFu) << 16);
}
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>

// ── Lua extraspace access (stores vgat_state_t*) ──────────────────────────
#if defined(HAVE_LUA)
#define VGATSTATE(L) ((vgat_state_t**)lua_getextraspace(L))

static int f_print(lua_State *L) {
  vgat_state_t *st = *VGATSTATE(L);
  for (int i = 1, n = lua_gettop(L); i <= n; i++) {
    vgat_screen_write_string(&st->screen, lua_tostring(L, i), VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
    if (i < n) vgat_screen_write_string(&st->screen, "\t", VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
  }
  vgat_screen_newline(&st->screen);
  return 0;
}

static int f_io_read(lua_State *L) { return lua_yield(L, 0); }

static int f_io_write(lua_State *L) {
  vgat_state_t *st = *VGATSTATE(L);
  for (int i = 1, n = lua_gettop(L); i <= n; i++) {
    const char *s = luaL_checkstring(L, i);
    vgat_screen_write_string(&st->screen, s, VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
    fprintf(stdout, "%s", s);
  }
  return 0;
}

static int f_stdout_write(lua_State *L) {
  vgat_state_t *st = *VGATSTATE(L);
  for (int i = 2, n = lua_gettop(L); i <= n; i++) {
    const char *s = luaL_checkstring(L, i);
    vgat_screen_write_string(&st->screen, s, VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
    fprintf(stdout, "%s", s);
  }
  lua_pushvalue(L, 1);
  return 1;
}

static int f_stdout_flush(lua_State *L) { lua_pushvalue(L, 1); return 1; }
static int f_stdout_setvbuf(lua_State *L) { lua_pushvalue(L, 1); return 1; }

static const char *STDOUT_METATABLE = "terminal.stdout";

static bool create_lua_state(vgat_state_t *st) {
  st->L = luaL_newstate();
  if (!st->L) return false;
  luaL_openlibs(st->L);

  *(vgat_state_t **)lua_getextraspace(st->L) = st;

  // Override print
  lua_pushcfunction(st->L, f_print);
  lua_setglobal(st->L, "print");

  // Setup stdout metatable
  luaL_newmetatable(st->L, STDOUT_METATABLE);
  lua_pushvalue(st->L, -1);                   lua_setfield(st->L, -2, "__index");
  lua_pushcfunction(st->L, f_stdout_write);   lua_setfield(st->L, -2, "write");
  lua_pushcfunction(st->L, f_stdout_flush);   lua_setfield(st->L, -2, "flush");
  lua_pushcfunction(st->L, f_stdout_setvbuf); lua_setfield(st->L, -2, "setvbuf");
  lua_pop(st->L, 1);

  lua_newuserdata(st->L, sizeof(void *));
  luaL_setmetatable(st->L, STDOUT_METATABLE);

  // Override io.*
  lua_getglobal(st->L, "io");
  lua_pushvalue(st->L, -2);             lua_setfield(st->L, -2, "output");
  lua_pushvalue(st->L, -2);             lua_setfield(st->L, -2, "stdout");
  lua_pushcfunction(st->L, f_io_write); lua_setfield(st->L, -2, "write");
  lua_pushcfunction(st->L, f_io_read);  lua_setfield(st->L, -2, "read");
  lua_pop(st->L, 2);

  return true;
}

static void continue_lua_coroutine(vgat_state_t *st, int nargs) {
  int nres;
  int status = lua_resume(st->co, NULL, nargs, &nres);

  if (status == LUA_OK) {
    vgat_screen_write_string(&st->screen, "\nProcess finished\n", 10, VGAT_BG_DEFAULT);
    st->lua_running = false;
    st->waiting_for_input = false;
  } else if (status == LUA_YIELD) {
    st->waiting_for_input = true;
    vgat_screen_write_string(&st->screen, "\n> ", VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
  } else {
    vgat_screen_write_string(&st->screen, "\nError: ", 9, VGAT_BG_DEFAULT);
    vgat_screen_write_string(&st->screen, lua_tostring(st->co, -1), VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
    vgat_screen_newline(&st->screen);
    st->lua_running = false;
    st->waiting_for_input = false;
  }
}

bool terminal_run_lua_file(vgat_state_t *st, const char *path) {
  // Create Lua state on first use
  if (!st->L) {
    if (!create_lua_state(st)) {
      vgat_screen_write_string(&st->screen, "Error: Failed to create Lua state\n", 9, VGAT_BG_DEFAULT);
      return false;
    }
  }

  // Create a coroutine and load the script
  st->co = lua_newthread(st->L);
  st->lua_running = true;
  st->waiting_for_input = false;

  if (luaL_loadfile(st->co, path) != LUA_OK) {
    vgat_screen_write_string(&st->screen, "Error loading file: ", 9, VGAT_BG_DEFAULT);
    vgat_screen_write_string(&st->screen, lua_tostring(st->co, -1), VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
    vgat_screen_newline(&st->screen);
    st->lua_running = false;
    return false;
  }

  continue_lua_coroutine(st, 0);
  return true;
}
#endif /* HAVE_LUA */

bool terminal_start_script(window_t *win, const char *path) {
  vgat_state_t *st = win ? (vgat_state_t *)win->userdata : NULL;
  if (!st || !path || !path[0]) return false;
#if defined(HAVE_LUA)
  free(st->startup_script);
  st->startup_script = strdup(path);
  if (!st->startup_script) return false;
  bool ok = terminal_run_lua_file(st, st->startup_script);
#else
  vgat_screen_write_string(&st->screen, "Lua scripting is not available in this build.\n",
                           9, VGAT_BG_DEFAULT);
  bool ok = false;
#endif
  invalidate_window(win);
  return ok;
}

// ── Command forward declarations ──────────────────────────────────────────

static void cmd_echo(vgat_state_t *, int, char **);
static void cmd_help(vgat_state_t *, int, char **);
static void cmd_clear(vgat_state_t *, int, char **);
static void cmd_dir(vgat_state_t *, int, char **);
static void cmd_pwd (vgat_state_t *, int, char **);
static void cmd_cd  (vgat_state_t *, int, char **);
static void cmd_cat (vgat_state_t *, int, char **);
static void cmd_date(vgat_state_t *, int, char **);
static void cmd_whoami(vgat_state_t *, int, char **);
static void cmd_lua  (vgat_state_t *, int, char **);
static void cmd_run  (vgat_state_t *, int, char **);
static void spawn_program(vgat_state_t *, char *const *);
static void enter_cmd_mode(vgat_state_t *);
static void init_ansi_parser(vgat_state_t *);
static bool start_pty_watch(vgat_state_t *);
static void stop_pty_watch(vgat_state_t *);
static void restart_cursor_blink(vgat_state_t *);
static void stop_cursor_blink(vgat_state_t *);

// ── Command table ─────────────────────────────────────────────────────────

const vgat_cmd_t g_cmds[] = {
  {"echo",   "Echo text",              cmd_echo},
  {"help",   "Show available commands",cmd_help},
  {"clear",  "Clear the console",      cmd_clear},
  {"dir",    "List directory contents", cmd_dir},
  {"ls",     "List directory contents", cmd_dir},
  {"pwd",    "Print working directory", cmd_pwd},
  {"cd",     "Change directory",       cmd_cd},
  {"cat",    "Display a file",         cmd_cat},
  {"date",   "Show date and time",     cmd_date},
  {"whoami", "Show current user",      cmd_whoami},
  {"lua",    "Run a Lua script",        cmd_lua},
  {"run",    "Run a program e.g. run ls -la", cmd_run},
  {NULL, NULL, NULL} // sentinel
};

// ── Command implementations ───────────────────────────────────────────────

static void cmd_echo(vgat_state_t *st, int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (i > 1) vgat_screen_write_string(&st->screen, " ", VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
    vgat_screen_write_string(&st->screen, argv[i], VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
  }
  vgat_screen_newline(&st->screen);
}

static void cmd_help(vgat_state_t *st, int argc, char **argv) {
  (void)argc; (void)argv;
  vgat_screen_write_string(&st->screen, "Available commands:\n", 10, VGAT_BG_DEFAULT);
  for (int i = 0; g_cmds[i].name; i++) {
    char line[128];
    int n = snprintf(line, sizeof(line), "  %-12s %s\n", g_cmds[i].name, g_cmds[i].help);
    if (n > 0) vgat_screen_write_string(&st->screen, line, VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
  }
}

static void cmd_clear(vgat_state_t *st, int argc, char **argv) {
  (void)argc; (void)argv;
  vgat_screen_clear(&st->screen);
}

static void cmd_dir(vgat_state_t *st, int argc, char **argv) {
  (void)argc;
  const char *path = argv[1] ? argv[1] : ".";
  DIR *d = opendir(path);
  if (!d) {
    vgat_screen_write_string(&st->screen, "Cannot open directory: ", 9, VGAT_BG_DEFAULT);
    vgat_screen_write_string(&st->screen, path, VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
    vgat_screen_newline(&st->screen);
    return;
  }
  struct dirent *e;
  while ((e = readdir(d))) {
    vgat_screen_write_string(&st->screen, e->d_name, VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
    vgat_screen_write_string(&st->screen, "  ", VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
  }
  closedir(d);
  vgat_screen_newline(&st->screen);
}

static void cmd_pwd(vgat_state_t *st, int argc, char **argv) {
  (void)argc; (void)argv;
  vgat_screen_write_string(&st->screen, st->cwd, VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
  vgat_screen_newline(&st->screen);
}

static void cmd_cd(vgat_state_t *st, int argc, char **argv) {
  (void)argc;
  const char *target = argv[1] ? argv[1] : getenv("HOME");
  if (!target) target = "/";

  // chdir to this terminal's cwd, then to target, capture result
  char savewd[1024];
  if (!getcwd(savewd, sizeof(savewd))) savewd[0] = '\0';
  chdir(st->cwd[0] ? st->cwd : "/");
  if (chdir(target) != 0) {
    vgat_screen_write_string(&st->screen, "cd: ", 9, VGAT_BG_DEFAULT);
    vgat_screen_write_string(&st->screen, target, VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
    vgat_screen_write_string(&st->screen, ": No such directory\n", 9, VGAT_BG_DEFAULT);
    if (savewd[0]) chdir(savewd);
    return;
  }
  if (!getcwd(st->cwd, sizeof(st->cwd)))
    snprintf(st->cwd, sizeof(st->cwd), "%s/%s", st->cwd, target);
  if (savewd[0]) chdir(savewd);
}

static void cmd_cat(vgat_state_t *st, int argc, char **argv) {
  if (argc < 2) {
    vgat_screen_write_string(&st->screen, "Usage: cat <file>\n", 9, VGAT_BG_DEFAULT);
    return;
  }
  FILE *fp = fopen(argv[1], "r");
  if (!fp) {
    vgat_screen_write_string(&st->screen, "cat: ", 9, VGAT_BG_DEFAULT);
    vgat_screen_write_string(&st->screen, argv[1], VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
    vgat_screen_write_string(&st->screen, ": No such file\n", 9, VGAT_BG_DEFAULT);
    return;
  }
  char buf[256];
  while (fgets(buf, sizeof(buf), fp)) {
    vgat_screen_write_string(&st->screen, buf, VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
  }
  fclose(fp);
}

static void cmd_date(vgat_state_t *st, int argc, char **argv) {
  (void)argc; (void)argv;
  time_t t = time(NULL);
  struct tm *tm = localtime(&t);
  char buf[64];
  if (strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Z %Y", tm)) {
    vgat_screen_write_string(&st->screen, buf, VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
    vgat_screen_newline(&st->screen);
  }
}

static void cmd_whoami(vgat_state_t *st, int argc, char **argv) {
  (void)argc; (void)argv;
  const char *user = getenv("USER");
  if (!user) user = getenv("USERNAME");
  if (!user) user = "unknown";
  vgat_screen_write_string(&st->screen, user, VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
  vgat_screen_newline(&st->screen);
}

static void cmd_lua(vgat_state_t *st, int argc, char **argv) {
#if defined(HAVE_LUA)
  if (argc < 2) {
    vgat_screen_write_string(&st->screen, "Usage: lua <script.lua>\n", 9, VGAT_BG_DEFAULT);
    return;
  }
  terminal_run_lua_file(st, argv[1]);
#else
  (void)st; (void)argc; (void)argv;
  vgat_screen_write_string(&st->screen, "Lua scripting is not available in this build.\n",
                           9, VGAT_BG_DEFAULT);
#endif
}

static void spawn_program(vgat_state_t *st, char *const *argv) {
  if (!argv || !argv[0]) return;

  // Kill any existing PTY session
  if (st->pty_fd >= 0) {
    enter_cmd_mode(st);
  }

  irect16_t cr = get_client_rect(st->win);
  int cols = cr.w / text_cell_width("Terminal");
  int rows = cr.h / text_char_height("Terminal");
  if (cols <= 0) cols = 80;
  if (rows <= 0) rows = 24;

  st->pty_fd = vgat_pty_exec((const char *const *)argv, rows, cols, &st->pty_pid);
  if (st->pty_fd < 0) {
    vgat_screen_write_string(&st->screen, "Failed to run: ", 9, VGAT_BG_DEFAULT);
    vgat_screen_write_string(&st->screen, argv[0], VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
    vgat_screen_newline(&st->screen);
    return;
  }

  st->mode = VGAT_MODE_PTY;
  st->escape_pending = false;
  init_ansi_parser(st);
  if (!start_pty_watch(st)) {
    vgat_pty_close(st->pty_pid); close(st->pty_fd);
    st->pty_fd = -1; st->pty_pid = 0; st->mode = VGAT_MODE_CMD;
    vgat_screen_write_string(&st->screen, "Failed to watch PTY output\n", 9, VGAT_BG_DEFAULT);
    return;
  }
  vgat_screen_clear(&st->screen);
}

static void cmd_run(vgat_state_t *st, int argc, char **argv) {
  if (argc < 2) {
    vgat_screen_write_string(&st->screen, "Usage: run <program> [args...]\n",
                             9, VGAT_BG_DEFAULT);
    return;
  }
  spawn_program(st, &argv[1]);
}

// ── Input processing ──────────────────────────────────────────────────────

static void process_input(vgat_state_t *st) {
  // Echo command to scrollback
  char *name = strrchr(st->cwd, '/');
  name = name ? name + 1 : st->cwd;
  vgat_screen_write_string(&st->screen, name, 10, VGAT_BG_DEFAULT);
  vgat_screen_write_string(&st->screen, " > ", 10, VGAT_BG_DEFAULT);
  vgat_screen_write_string(&st->screen, st->input_buf, VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
  vgat_screen_newline(&st->screen);

  // Trim leading whitespace
  char *p = st->input_buf;
  while (*p == ' ' || *p == '\t') p++;
  if (!*p) return;

  // Parse argv (max 64 args, modifies string in-place)
  int argc = 0;
  char *argv[65];
  bool in = false;
  for (char *q = p; *q && argc < 64; q++) {
    if (*q == ' ' || *q == '\t') {
      *q = '\0';
      in = false;
    } else if (!in) {
      argv[argc++] = q;
      in = true;
    }
  }
  argv[argc] = NULL;

  // chdir to this terminal's working directory for filesystem commands
  char oldwd[1024];
  if (!getcwd(oldwd, sizeof(oldwd))) oldwd[0] = '\0';
  if (st->cwd[0]) chdir(st->cwd);

  // Dispatch
  for (int i = 0; g_cmds[i].name; i++) {
    if (strcmp(argv[0], g_cmds[i].name) == 0) {
      g_cmds[i].func(st, argc, argv);
      if (oldwd[0]) chdir(oldwd);
      return;
    }
  }

  // No built-in match — try as a program name directly
  spawn_program(st, argv);
  if (oldwd[0]) chdir(oldwd);
}

// ── Mode switching helpers ──────────────────────────────────────────────

static bool start_pty_watch(vgat_state_t *st) {
  st->pty_generation++;
  if (!st->pty_generation) st->pty_generation++;
  st->pty_watch = vgat_pty_watch_start(st->pty_fd, st->win,
                                       evTerminalPtyReady, st->pty_generation);
  return st->pty_watch != NULL;
}

static void stop_pty_watch(vgat_state_t *st) {
  vgat_pty_watch_stop(st->pty_watch);
  st->pty_watch = NULL;
}

static void restart_cursor_blink(vgat_state_t *st) {
  st->cursor_visible = true;
  if (!window_has_focus(st->win)) return;
  if (st->cursor_timer_id) axCancelTimer(st->cursor_timer_id);
  st->cursor_timer_id = axSetTimer(st->win, VGAT_CURSOR_BLINK_MS, NULL, true);
}

static void stop_cursor_blink(vgat_state_t *st) {
  if (st->cursor_timer_id) axCancelTimer(st->cursor_timer_id);
  st->cursor_timer_id = 0;
  st->cursor_visible = false;
}

static void enter_cmd_mode(vgat_state_t *st) {
  stop_pty_watch(st);
  if (st->pty_fd >= 0) {
    vgat_pty_close(st->pty_pid);
    close(st->pty_fd);
    st->pty_fd = -1;
    st->pty_pid = 0;
  }
  st->mode = VGAT_MODE_CMD;
  st->escape_pending = false;
  vgat_screen_write_string(&st->screen, "\n[returned to command mode]\n",
                           VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
  vgat_screen_newline(&st->screen);
}

// ── ANSI parser callbacks ────────────────────────────────────────────────

static void on_set_title(void *ud, const char *title) {
  window_t *win = (window_t *)ud;
  if (!win || !title) return;
  snprintf(win->title, sizeof(win->title), "%s", title);
  invalidate_window(win);
}

static void on_set_palette_color(void *ud, int idx, uint32_t rgba) {
  window_t *win = (window_t *)ud;
  if (!win || !win->userdata) return;
  vgat_state_t *st = (vgat_state_t *)win->userdata;
  if (idx >= 0 && idx <= 255) {
    kAnsi256[idx] = rgba;
    st->palette_dirty = true;
    invalidate_window(win);
  }
}

static void on_reset_palette(void *ud) {
  window_t *win = (window_t *)ud;
  if (!win || !win->userdata) return;
  vgat_state_t *st = (vgat_state_t *)win->userdata;
  ansi_palette_reset();
  st->palette_dirty = true;
  invalidate_window(win);
}

static void init_ansi_parser(vgat_state_t *st) {
  vgat_parser_init(&st->parser, &(vgat_parser_callbacks_t){
    .screen = &st->screen,
    .write_cell = vgat_screen_write_cell,
    .newline = vgat_screen_newline,
    .backspace = vgat_screen_backspace,
    .cr = vgat_screen_carriage_return,
    .cursor_left = vgat_screen_cursor_left,
    .cursor_right = vgat_screen_cursor_right,
    .cursor_up = vgat_screen_cursor_up,
    .cursor_down = vgat_screen_cursor_down,
    .set_fg = vgat_screen_set_fg,
    .set_bg = vgat_screen_set_bg,
    .reset = vgat_screen_reset,
    .save_cursor = vgat_screen_save_cursor,
    .restore_cursor = vgat_screen_restore_cursor,
    .erase_display = vgat_screen_erase_display,
    .erase_line = vgat_screen_erase_line,
    .cursor_pos = vgat_screen_cursor_position,
    .set_title = on_set_title,
    .set_palette_color = on_set_palette_color,
    .reset_palette = on_reset_palette,
  });
  st->parser.userdata = st->win;
}

// ── Window procedure ──────────────────────────────────────────────────────

result_t terminal_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  vgat_state_t *st = (vgat_state_t *)win->userdata;

  switch (msg) {

    case evCreate: {
      st = (vgat_state_t *)calloc(1, sizeof(vgat_state_t));
      win->userdata = st;
      if (!st) return false;
      st->win = win;
      st->default_fg = kAnsi16[VGAT_FG_DEFAULT];
      st->default_bg = kAnsi16[VGAT_BG_DEFAULT];
      st->scroll_pos = 0;
      st->cursor_visible = true;
      st->pty_fd = -1;
      if (!getcwd(st->cwd, sizeof(st->cwd)))
        snprintf(st->cwd, sizeof(st->cwd), "/");

      irect16_t cr = get_client_rect(win);
      int cols = cr.w / text_cell_width("Terminal");
      int rows = cr.h / text_char_height("Terminal");
      if (cols <= 0) cols = VGAT_DEFAULT_COLS;
      if (rows <= 0) rows = VGAT_DEFAULT_ROWS;

      vgat_screen_init(&st->screen, rows, cols);

      // Welcome message
      vgat_screen_write_string(&st->screen, "Terminal v1.0\n", 10, VGAT_BG_DEFAULT);
      vgat_screen_write_string(&st->screen, "Type 'help' for available commands\n", 10, VGAT_BG_DEFAULT);
      vgat_screen_newline(&st->screen);

      (void)lparam;
      return true;
    }

    case evSetFocus: {
      if (!st) return true;
      restart_cursor_blink(st);
      invalidate_window(win);
      return true;
    }

    case evKillFocus: {
      if (!st) return true;
      stop_cursor_blink(st);
      invalidate_window(win);
      return true;
    }

    case evDestroy: {
      if (st) {
        stop_cursor_blink(st);
        stop_pty_watch(st);
        if (st->pty_fd >= 0) {
          close(st->pty_fd);
          st->pty_fd = -1;
          vgat_pty_close(st->pty_pid);
        }
        vgat_screen_shutdown(&st->screen);
#if defined(HAVE_LUA)
        if (st->L) lua_close(st->L);
#endif
        free(st->startup_script);
        free(st);
        win->userdata = NULL;
      }
      return false;
    }

    case evTimer: {
      if (!st) return true;
      if (wparam == st->cursor_timer_id && st->cursor_timer_id) {
        st->cursor_visible = !st->cursor_visible;
        invalidate_window(win);
        return true;
      }
      return false;
    }

    case evTerminalPtyReady: {
      if (!st || wparam != st->pty_generation || !st->pty_watch ||
          st->mode != VGAT_MODE_PTY || st->pty_fd < 0) return false;
      // Drain a bounded batch: enough throughput without starving the UI.
      bool dirty = false;
      for (int reads = 0; reads < 16; reads++) {
        int n = vgat_pty_read(st->pty_fd, st->read_buf, sizeof(st->read_buf));
        if (n > 0) {
          vgat_parser_feed(&st->parser, (uint8_t *)st->read_buf, n);
          dirty = true;
          continue;
        }
        if (n == 0 || (n < 0 && errno != EAGAIN)) enter_cmd_mode(st);
        break;
      }
      if (st->mode == VGAT_MODE_PTY) vgat_pty_watch_rearm(st->pty_watch);
      if (dirty || st->mode != VGAT_MODE_PTY) invalidate_window(win);
      return true;
    }

    case evPaint: {
      if (!st || !st->screen.rows) return true;

      int cw = text_cell_width("Terminal");
      int ch = text_char_height("Terminal");
      irect16_t cr = get_client_rect(win);
      int vis_cols = cr.w / cw;
      int vis_rows = cr.h / ch;
      if (vis_cols <= 0 || vis_rows <= 0) return true;

      // One row of cells at a time, in xterm colours.
      static text_cell_t line[1024];
      if (vis_cols > (int)(sizeof(line) / sizeof(line[0]))) vis_cols = (int)(sizeof(line) / sizeof(line[0]));
      uint32_t def_fg = terminal_color(VGAT_FG_DEFAULT), def_bg = terminal_color(VGAT_BG_DEFAULT);
      fill_rect(def_bg, cr);

      // In PTY mode, use all rows for output. In CMD mode, reserve last row for input.
      int content_rows = (st->mode == VGAT_MODE_PTY) ? vis_rows : vis_rows - 1;
      if (content_rows > 0) {
        int written = (st->mode == VGAT_MODE_PTY) ? st->screen.max_row : st->screen.cursor_row;
        int first = written - content_rows - st->scroll_pos;
        if (first < 0) first = 0;
        bool show_cursor = window_has_focus(win) && st->mode == VGAT_MODE_PTY && st->cursor_visible && st->screen.cursor_visible;

        for (int row = 0; row < content_rows; row++) {
          int logical = first + row;
          if (logical >= written) break;
          int phys = (st->screen.head + logical) % st->screen.total_rows;
          int cols = MIN(vis_cols, st->screen.cols);
          for (int col = 0; col < cols; col++) {
            vgat_cell *cell = &st->screen.rows[phys * st->screen.cols + col];
            bool cursor = show_cursor && logical == st->screen.cursor_row && col == st->screen.cursor_col;   // a block: colours swapped
            line[col] = (text_cell_t){ cell->codepoint, terminal_color(cursor ? cell->bg : cell->fg),
                                       terminal_color(cursor ? cell->fg : cell->bg) };
          }
          draw_text_cells("Terminal", line, cols, cr.x, cr.y + row * ch);
        }
      }

      // ── Input line (last row, CMD mode only) ──
      if (st->mode != VGAT_MODE_PTY) {
        int input_row = vis_rows - 1;
        int col = 0;
        const char *prompt = "> ";
        if (st->cwd[0]) {
          char *name = strrchr(st->cwd, '/');
          name = name ? name + 1 : st->cwd;
          static char pbuf[256];
          snprintf(pbuf, sizeof(pbuf), "%s > ", name);
          prompt = pbuf;
        }
        for (const char *cp = prompt; *cp && col < vis_cols; cp++, col++)
          line[col] = (text_cell_t){ (unsigned char)*cp, terminal_color(10), def_bg };
        for (int i = 0; i < st->input_len && col < vis_cols; i++, col++)
          line[col] = (text_cell_t){ (unsigned char)st->input_buf[i], def_fg, def_bg };
        if (window_has_focus(win) && st->cursor_visible && col < vis_cols)
          line[col++] = (text_cell_t){ ' ', def_bg, def_fg };
        draw_text_cells("Terminal", line, col, cr.x, cr.y + input_row * ch);
      }
      return true;
    }

    case evArrange: {
      layout_arrange_t *a = (layout_arrange_t *)lparam;
      if (a) win->frame = a->rect;
      terminal_proc(win, evResize, 0, NULL);
      return true;
    }

    case evResize: {
      if (!st) return false;
      irect16_t cr = get_client_rect(win);
      int cols = cr.w / text_cell_width("Terminal");
      int rows = cr.h / text_char_height("Terminal");
      if (cols <= 0 || rows <= 0) return false;

      vgat_screen_resize(&st->screen, cols);

      // Signal PTY about new terminal size
      if (st->mode == VGAT_MODE_PTY && st->pty_fd >= 0) {
        vgat_pty_resize(st->pty_fd, rows, cols);
      }

      int content_rows = (st->mode == VGAT_MODE_PTY) ? rows : rows - 1;
      int written = (st->mode == VGAT_MODE_PTY) ? st->screen.max_row : st->screen.cursor_row;
      int max_scroll = written > content_rows ? written - content_rows : 0;
      scroll_info_t si = {
        .fMask = SIF_RANGE | SIF_PAGE | SIF_POS,
        .nMin = 0,
        .nMax = max_scroll,
        .nPage = (uint32_t)content_rows,
        .nPos = st->scroll_pos,
      };
      set_scroll_info(win, SB_VERT, &si, true);
      invalidate_window(win);
      return false;
    }

    case evVScroll: {
      if (!st) return false;
      irect16_t cr = get_client_rect(win);
      int vis_rows = cr.h / text_char_height("Terminal");
      if (vis_rows <= 0) vis_rows = 24;
      int content_rows = (st->mode == VGAT_MODE_PTY) ? vis_rows : vis_rows - 1;
      int written = (st->mode == VGAT_MODE_PTY) ? st->screen.max_row : st->screen.cursor_row;
      int max_scroll = written > content_rows ? written - content_rows : 0;
      int new_pos = CLAMP((int)wparam, 0, max_scroll);
      if (new_pos != st->scroll_pos) {
        st->scroll_pos = new_pos;
        scroll_info_t si = { .fMask = SIF_POS, .nPos = st->scroll_pos };
        set_scroll_info(win, SB_VERT, &si, false);
        invalidate_window(win);
      }
      return true;
    }

    case evWheel: {
      if (!st) return false;
      int delta = (int16_t)HIWORD((uintptr_t)lparam);
      irect16_t cr = get_client_rect(win);
      int vis_rows = cr.h / text_char_height("Terminal");
      if (vis_rows <= 0) vis_rows = 24;
      int content_rows = (st->mode == VGAT_MODE_PTY) ? vis_rows : vis_rows - 1;
      int written = (st->mode == VGAT_MODE_PTY) ? st->screen.max_row : st->screen.cursor_row;
      int max_scroll = written > content_rows ? written - content_rows : 0;
      int lines = delta < 0 ? 3 : -3;
      int new_pos = CLAMP(st->scroll_pos + lines, 0, max_scroll);
      if (new_pos != st->scroll_pos) {
        st->scroll_pos = new_pos;
        scroll_info_t si = { .fMask = SIF_POS, .nPos = st->scroll_pos };
        set_scroll_info(win, SB_VERT, &si, false);
        invalidate_window(win);
      }
      return true;
    }

    case evKeyDown: {
      if (!st) return false;

      // PTY mode: forward keys to child process
      if (st->mode == VGAT_MODE_PTY && st->pty_fd >= 0) {
        int base = wparam & 0xFF;
        int mods = (int)ui_get_mod_state();
        if ((mods & AX_MOD_CTRL) || base < 0x20 || base >= 0x7F)
          restart_cursor_blink(st); // Printable keys reset on evTextInput instead.

        // Ctrl+letter → control character
        if (mods & AX_MOD_CTRL) {
          if (base == 'A' || base == 'a') {
            // Ctrl-A: escape chord — if pressed again, send literal Ctrl-A
            if (st->escape_pending) {
              char ctrl = '\x01';
              vgat_pty_write(st->pty_fd, &ctrl, 1);
              st->escape_pending = false;
            } else {
              st->escape_pending = true;
            }
            return true;
          }
          // If escape is pending and any other Ctrl key is pressed, cancel escape
          st->escape_pending = false;
          if (base >= 'A' && base <= 'Z') {
            char ctrl = (char)(base - 'A' + 1);
            vgat_pty_write(st->pty_fd, &ctrl, 1);
            return true;
          }
          if (base >= 'a' && base <= 'z') {
            char ctrl = (char)(base - 'a' + 1);
            vgat_pty_write(st->pty_fd, &ctrl, 1);
            return true;
          }
        }

        // If escape is pending, check for escape chord completion
        if (st->escape_pending) {
          st->escape_pending = false;
          if (base == 'q' || base == 'Q') {
            enter_cmd_mode(st);
            invalidate_window(win);
            return true;
          }
          // Cancel escape and send literal Ctrl-A + the key
          char ctrl_a = '\x01';
          vgat_pty_write(st->pty_fd, &ctrl_a, 1);
          // Fall through to send the key too
        }

        // Alt+key → ESC prefix
        if (mods & AX_MOD_ALT) {
          // For special keys, we handle Alt as escape prefix below
          // For regular keys, evTextInput will handle the ESC+char sequence
        }

        // Special keys — formatted as a compact table
        switch (base) {
          case AX_KEY_ENTER:     vgat_pty_write(st->pty_fd, "\r", 1);                          return true;
          case AX_KEY_BACKSPACE: vgat_pty_write(st->pty_fd, "\x7f", 1);                        return true;
          case AX_KEY_ESCAPE:    vgat_pty_write(st->pty_fd, "\x1b", 1);                        return true;
          case AX_KEY_TAB:       vgat_pty_write(st->pty_fd, "\t", 1);                          return true;
          case AX_KEY_UPARROW:   vgat_pty_write(st->pty_fd, mods & AX_MOD_SHIFT ? "\x1b[1;2A" : "\x1b[A", 3); return true;
          case AX_KEY_DOWNARROW: vgat_pty_write(st->pty_fd, mods & AX_MOD_SHIFT ? "\x1b[1;2B" : "\x1b[B", 3); return true;
          case AX_KEY_RIGHTARROW:vgat_pty_write(st->pty_fd, mods & AX_MOD_SHIFT ? "\x1b[1;2C" : "\x1b[C", 3); return true;
          case AX_KEY_LEFTARROW: vgat_pty_write(st->pty_fd, mods & AX_MOD_SHIFT ? "\x1b[1;2D" : "\x1b[D", 3); return true;
          case AX_KEY_HOME:      vgat_pty_write(st->pty_fd, "\x1b[H", 3);                      return true;
          case AX_KEY_END:       vgat_pty_write(st->pty_fd, "\x1b[F", 3);                      return true;
          case AX_KEY_DEL:       vgat_pty_write(st->pty_fd, "\x1b[3~", 4);                     return true;
          case AX_KEY_PGUP:      vgat_pty_write(st->pty_fd, "\x1b[5~", 4);                     return true;
          case AX_KEY_PGDN:      vgat_pty_write(st->pty_fd, "\x1b[6~", 4);                     return true;
          case AX_KEY_F1:        vgat_pty_write(st->pty_fd, "\x1bOP", 3);                      return true;
          case AX_KEY_F2:        vgat_pty_write(st->pty_fd, "\x1bOQ", 3);                      return true;
          case AX_KEY_F3:        vgat_pty_write(st->pty_fd, "\x1bOR", 3);                      return true;
          case AX_KEY_F4:        vgat_pty_write(st->pty_fd, "\x1bOS", 3);                      return true;
          default:
            // For regular alphanumeric keys, let evTextInput handle them
            if (base >= 0x20 && base < 0x7F) {
              // If Alt is held, prepend ESC before the character
              if (mods & AX_MOD_ALT) {
                vgat_pty_write(st->pty_fd, "\x1b", 1);
                // Let evTextInput send the character after the ESC
                return false;
              }
              return false;
            }
            // Eat all other keys
            return true;
        }
      }

      // CMD / Lua mode: existing command processing
      switch (wparam) {
        case AX_KEY_ENTER:
          restart_cursor_blink(st);
          if (st->lua_running && st->waiting_for_input) {
            vgat_screen_write_string(&st->screen, "> ", 10, VGAT_BG_DEFAULT);
            vgat_screen_write_string(&st->screen, st->input_buf, VGAT_FG_DEFAULT, VGAT_BG_DEFAULT);
            vgat_screen_newline(&st->screen);
#if defined(HAVE_LUA)
            lua_pushstring(st->co, st->input_buf);
            continue_lua_coroutine(st, 1);
#endif
          } else {
            process_input(st);
          }
          st->input_len = 0;
          st->input_buf[0] = '\0';
          invalidate_window(win);
          return true;
        case AX_KEY_BACKSPACE:
          restart_cursor_blink(st);
          if (st->input_len > 0) {
            st->input_buf[--st->input_len] = '\0';
            invalidate_window(win);
          }
          return true;
        default:
          return false;
      }
    }

    case evTextInput: {
      if (!st) return false;
      restart_cursor_blink(st);
      // In PTY mode, forward text directly to the child process
      if (st->mode == VGAT_MODE_PTY && st->pty_fd >= 0) {
        const char *text = (const char *)lparam;
        vgat_pty_write(st->pty_fd, text, (int)strlen(text));
        return true;
      }
      // In CMD mode, append to input buffer (existing behavior)
      const char *text = (const char *)lparam;
      while (*text && st->input_len < VGAT_INPUT_MAX - 1) {
        if (*text >= 0x20 && *text < 0x7F) {
          st->input_buf[st->input_len++] = *text;
        }
        text++;
      }
      st->input_buf[st->input_len] = '\0';
      invalidate_window(win);
      return true;
    }

    default:
      return 0;
  }
}

// ── Test hook ─────────────────────────────────────────────────────────────
int terminal_get_cursor_row(window_t *win) {
  if (!win || !win->userdata) return -1;
  vgat_state_t *st = (vgat_state_t *)win->userdata;
  return st->screen.cursor_row;
}
