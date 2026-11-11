// PTY (pseudo-terminal) abstraction layer.
// Unix: forkpty()
// Windows: ConPTY ( stubs for now)

#ifndef __VGAT_PTY_H__
#define __VGAT_PTY_H__

#include <stdbool.h>
#include <stdint.h>

typedef struct vgat_pty_watch_s vgat_pty_watch_t;

// Open a new PTY and fork a shell process.
// shell: path to shell binary (NULL = $SHELL or /bin/sh)
// rows, cols: initial terminal size
// pid_out: set to child process ID on success
// Returns: master fd on success, -1 on failure
int vgat_pty_open(const char *shell, int rows, int cols, int *pid_out);

// Open a new PTY and exec an arbitrary program.
// argv: NULL-terminated argument vector (argv[0] = program path)
// cwd: working directory of the program (NULL = inherit)
// rows, cols: initial terminal size
// pid_out: set to child process ID on success
// Returns: master fd on success, -1 on failure
int vgat_pty_exec(const char *const *argv, const char *cwd, int rows, int cols, int *pid_out);

// Reap the child, terminating its process group first if it still runs.
// Returns its exit code, 128 + signal when a signal ended it, -1 without a child.
int vgat_pty_close(int pid);

// Ask the child's process group to terminate (SIGTERM) without waiting.
void vgat_pty_terminate(int pid);

// Resize the terminal (signal child via TIOCSWINSZ on Unix).
bool vgat_pty_resize(int master_fd, int rows, int cols);

// Read bytes from the PTY master (non-blocking).
// Returns: number of bytes read, 0 if nothing available, -1 on error.
int vgat_pty_read(int master_fd, void *buf, int sz);

// Write bytes to the PTY master (to send to the shell).
// Returns: number of bytes written, -1 on error.
int vgat_pty_write(int master_fd, const void *buf, int sz);

// Wake the UI event loop when the PTY becomes readable. The watcher never
// consumes PTY bytes; the window procedure remains the sole reader/parser.
vgat_pty_watch_t *vgat_pty_watch_start(int master_fd, void *target,
                                       uint32_t event, uint32_t token);
void vgat_pty_watch_rearm(vgat_pty_watch_t *watch);
void vgat_pty_watch_stop(vgat_pty_watch_t *watch);

#endif // __VGAT_PTY_H__
