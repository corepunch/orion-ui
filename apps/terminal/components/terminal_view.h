// TerminalView — the Terminal app's emulator as a reusable control.
//
// It ships in the terminal_components plugin. Other apps load that plugin
// (fe_load_component_plugin) and drive the view only through these messages,
// so they need this header and no link dependency on the terminal sources.

#ifndef __TERMINAL_VIEW_H__
#define __TERMINAL_VIEW_H__

#include <orion/ui.h>

#define TERMINAL_VIEW_CLASS "TerminalView"

// lparam of termExec: a shell command line, run as `/bin/sh -c command`.
typedef struct {
  const char *command;
  const char *cwd;      // NULL = the view's current directory
} terminal_exec_t;

enum {
  termExec = evUser + 1300, // lparam=const terminal_exec_t*; clears the screen, echoes the command and runs it
                            // in a PTY; returns true when started (false while another program runs)
  termKill,                 // wparam=1 forces SIGKILL; otherwise SIGTERM to the program's process group
  termIsRunning,            // returns true while a program runs
  termGetExitCode,          // the last termExec program's exit code (128 + signal); -1 before one ends
  termRunScript,            // lparam=const char* path of a Lua script to run in the view
};

// Notification (evCommand to the root window, HIWORD = code, lparam = the view):
// a program started by termExec ended; termGetExitCode gives its code.
#define termnExited 0x7510

#endif
