#include "terminal_pty.h"
#include "vgat.h"

#if defined(__unix__) || defined(__APPLE__)

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#if defined(__APPLE__)
#include <util.h>
#elif defined(__linux__)
#include <pty.h>
#else
#include <libutil.h>
#endif

struct vgat_pty_watch_s {
  int fd, wake_read, wake_write;
  void *target;
  uint32_t event, token;
  pthread_t thread;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  bool stop, pending;
};

static void *pty_watch_thread(void *arg) {
  vgat_pty_watch_t *w = (vgat_pty_watch_t *)arg;
  for (;;) {
    struct pollfd fds[] = {
      { .fd = w->fd,        .events = POLLIN },
      { .fd = w->wake_read, .events = POLLIN },
    };
    int n = poll(fds, 2, -1);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0 || fds[1].revents) break;
    if (!(fds[0].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL))) continue;

    pthread_mutex_lock(&w->mutex);
    if (!w->stop && !w->pending) {
      w->pending = true;
      pthread_mutex_unlock(&w->mutex);
      axPostMessageW(w->target, w->event, w->token, NULL);
      pthread_mutex_lock(&w->mutex);
    }
    while (w->pending && !w->stop) pthread_cond_wait(&w->cond, &w->mutex);
    bool stop = w->stop;
    pthread_mutex_unlock(&w->mutex);
    if (stop) break;
  }
  return NULL;
}

vgat_pty_watch_t *vgat_pty_watch_start(int master_fd, void *target,
                                       uint32_t event, uint32_t token) {
  if (master_fd < 0 || !target) return NULL;
  vgat_pty_watch_t *w = (vgat_pty_watch_t *)calloc(1, sizeof(*w));
  if (!w) return NULL;
  int wake[2];
  if (pipe(wake) != 0) { free(w); return NULL; }
  w->fd = master_fd; w->wake_read = wake[0]; w->wake_write = wake[1];
  w->target = target; w->event = event; w->token = token;
  pthread_mutex_init(&w->mutex, NULL);
  pthread_cond_init(&w->cond, NULL);
  if (pthread_create(&w->thread, NULL, pty_watch_thread, w) != 0) {
    pthread_cond_destroy(&w->cond); pthread_mutex_destroy(&w->mutex);
    close(w->wake_read); close(w->wake_write); free(w); return NULL;
  }
  return w;
}

void vgat_pty_watch_rearm(vgat_pty_watch_t *w) {
  if (!w) return;
  pthread_mutex_lock(&w->mutex);
  w->pending = false;
  pthread_cond_signal(&w->cond);
  pthread_mutex_unlock(&w->mutex);
}

void vgat_pty_watch_stop(vgat_pty_watch_t *w) {
  if (!w) return;
  pthread_mutex_lock(&w->mutex);
  w->stop = true; w->pending = false;
  pthread_cond_broadcast(&w->cond);
  pthread_mutex_unlock(&w->mutex);
  (void)write(w->wake_write, "x", 1);
  pthread_join(w->thread, NULL);
  pthread_cond_destroy(&w->cond); pthread_mutex_destroy(&w->mutex);
  close(w->wake_read); close(w->wake_write); free(w);
}

int vgat_pty_open(const char *shell, int rows, int cols, int *pid_out) {
  if (!pid_out) return -1;

  struct winsize ws = { .ws_row = (unsigned short)rows, .ws_col = (unsigned short)cols };
  int master_fd = -1;
  int pid = forkpty(&master_fd, NULL, NULL, &ws);
  if (pid == 0) {
    // Child process
    const char *sh = shell ? shell : (getenv("SHELL") ? getenv("SHELL") : "/bin/sh");
    execl(sh, sh, NULL);
    _exit(127);
  }

  if (pid < 0) {
    if (master_fd >= 0) close(master_fd);
    return -1;
  }

  // Set non-blocking on master
  int flags = fcntl(master_fd, F_GETFL, 0);
  if (flags >= 0) fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);

  // Set initial window size
  vgat_pty_resize(master_fd, rows, cols);

  *pid_out = pid;
  return master_fd;
}

int vgat_pty_exec(const char *const *argv, const char *cwd, int rows, int cols, int *pid_out) {
  if (!pid_out || !argv || !argv[0]) return -1;

  struct winsize ws = { .ws_row = (unsigned short)rows, .ws_col = (unsigned short)cols };
  int master_fd = -1;
  int pid = forkpty(&master_fd, NULL, NULL, &ws);
  if (pid == 0) {
    setenv("TERM", "linux", 1);
    if (cwd && cwd[0] && chdir(cwd) != 0) {
      fprintf(stderr, "%s: %s\r\n", cwd, strerror(errno));
      _exit(127);
    }
    execvp(argv[0], (char *const *)argv);
    fprintf(stderr, "%s: %s\r\n", argv[0], strerror(errno));
    _exit(127);
  }

  if (pid < 0) {
    if (master_fd >= 0) close(master_fd);
    return -1;
  }

  int flags = fcntl(master_fd, F_GETFL, 0);
  if (flags >= 0) fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);

  vgat_pty_resize(master_fd, rows, cols);

  *pid_out = pid;
  return master_fd;
}

static int pty_exit_code(int status) {
  return WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1;
}

// forkpty makes the child a session leader, so its pid is also its process group.
void vgat_pty_terminate(int pid) {
  if (pid > 0 && kill(-pid, SIGTERM) != 0) kill(pid, SIGTERM);
}

int vgat_pty_close(int pid) {
  if (pid <= 0) return -1;
  int status;
  if (waitpid(pid, &status, WNOHANG) == pid) return pty_exit_code(status);
  // Still running: SIGTERM, wait briefly, then SIGKILL.
  vgat_pty_terminate(pid);
  for (int i = 0; i < 10; i++) {
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) return pty_exit_code(status);
    if (r < 0) return -1;
    usleep(1000);  // 1ms × 10 = 10ms total
  }
  if (kill(-pid, SIGKILL) != 0) kill(pid, SIGKILL);
  return waitpid(pid, &status, 0) == pid ? pty_exit_code(status) : -1;
}

bool vgat_pty_resize(int master_fd, int rows, int cols) {
  if (master_fd < 0 || rows <= 0 || cols <= 0) return false;
  struct winsize ws = { .ws_row = (unsigned short)rows, .ws_col = (unsigned short)cols };
  return ioctl(master_fd, TIOCSWINSZ, &ws) == 0;
}

int vgat_pty_read(int master_fd, void *buf, int sz) {
  if (master_fd < 0 || !buf || sz <= 0) return -1;
  return read(master_fd, buf, (size_t)sz);
}

int vgat_pty_write(int master_fd, const void *buf, int sz) {
  if (master_fd < 0 || !buf || sz <= 0) return -1;
  return write(master_fd, buf, (size_t)sz);
}

#elif defined(_WIN32) || defined(_WIN64)

// Windows PTY stubs — ConPTY integration is more involved
// and requires creating a Pseudo Console and attaching pipes.

int vgat_pty_open(const char *shell, int rows, int cols, int *pid_out) {
  (void)shell; (void)rows; (void)cols; (void)pid_out;
  return -1;  // Not yet implemented on Windows
}

int vgat_pty_exec(const char *const *argv, const char *cwd, int rows, int cols, int *pid_out) {
  (void)argv; (void)cwd; (void)rows; (void)cols; (void)pid_out;
  return -1;  // Not yet implemented on Windows
}

int vgat_pty_close(int pid) {
  (void)pid;
  return -1;
}

void vgat_pty_terminate(int pid) { (void)pid; }

bool vgat_pty_resize(int master_fd, int rows, int cols) {
  (void)master_fd; (void)rows; (void)cols;
  return false;
}

int vgat_pty_read(int master_fd, void *buf, int sz) {
  (void)master_fd; (void)buf; (void)sz;
  return -1;
}

int vgat_pty_write(int master_fd, const void *buf, int sz) {
  (void)master_fd; (void)buf; (void)sz;
  return -1;
}

vgat_pty_watch_t *vgat_pty_watch_start(int master_fd, void *target,
                                       uint32_t event, uint32_t token) {
  (void)master_fd; (void)target; (void)event; (void)token;
  return NULL;
}

void vgat_pty_watch_rearm(vgat_pty_watch_t *watch) { (void)watch; }
void vgat_pty_watch_stop(vgat_pty_watch_t *watch) { (void)watch; }

#else

#error "Unsupported platform for PTY"

#endif
