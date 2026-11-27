#include "process.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>

struct ui_process { HANDLE handle; int status; bool exited; };

static bool process_command_line(const char *const *argv, wchar_t *line, size_t capacity) {
  size_t used = 0;
  for (size_t i = 0; argv[i]; i++) {
    wchar_t arg[32768];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[i], -1, arg, 32768)) return false;
    if (used + 2 >= capacity) return false;
    if (i) line[used++] = L' ';
    line[used++] = L'"';
    const wchar_t *p = arg;
    for (;;) {
      size_t slashes = 0;
      while (*p == L'\\') { slashes++; p++; }
      if (!*p || *p == L'"') slashes *= 2;
      if (*p == L'"') slashes++;
      if (used + slashes + 2 >= capacity) return false;
      while (slashes--) line[used++] = L'\\';
      if (!*p) break;
      line[used++] = *p++;
    }
    line[used++] = L'"';
  }
  line[used] = L'\0';
  return true;
}

ui_process_t *ui_process_start(const char *const *argv) {
  wchar_t line[32768];
  if (!argv || !argv[0] || !process_command_line(argv, line, 32768)) {
    fprintf(stderr, "[process] invalid or oversized command arguments\n"); fflush(stderr);
    return NULL;
  }
  ui_process_t *process = calloc(1, sizeof(*process));
  if (!process) { fprintf(stderr, "[process] cannot allocate a child handle\n"); fflush(stderr); return NULL; }
  STARTUPINFOW startup = { .cb = sizeof(startup) };
  PROCESS_INFORMATION child;
  wchar_t executable[32768], path[32768], name[1024];
  DWORD length = GetEnvironmentVariableW(L"PATH", path, 32768);
  DWORD resolved = 0;
  if (length && length < 32768 && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[0], -1, name, 1024))
    resolved = SearchPathW(path, name, L".exe", 32768, executable, NULL);
  if (!resolved || resolved >= 32768 ||
      !CreateProcessW(executable, line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &child)) {
    fprintf(stderr, "[process] cannot start %s (Windows error %lu)\n", argv[0], (unsigned long)GetLastError()); fflush(stderr);
    free(process);
    return NULL;
  }
  CloseHandle(child.hThread);
  process->handle = child.hProcess;
  return process;
}

bool ui_process_poll(ui_process_t *process, int *exit_code) {
  if (!process->exited) {
    DWORD state = WaitForSingleObject(process->handle, 0), code;
    if (state == WAIT_TIMEOUT) return false;
    process->exited = true;
    process->status = state == WAIT_OBJECT_0 && GetExitCodeProcess(process->handle, &code) && code <= INT_MAX ? (int)code : -1;
  }
  if (exit_code) *exit_code = process->status;
  return true;
}

void ui_process_close(ui_process_t *process) {
  if (!process) return;
  if (!ui_process_poll(process, NULL)) {
    TerminateProcess(process->handle, 1);
    WaitForSingleObject(process->handle, INFINITE);
  }
  CloseHandle(process->handle);
  free(process);
}
#else
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <string.h>

extern char **environ;
struct ui_process { pid_t pid; int status; bool exited; };

ui_process_t *ui_process_start(const char *const *argv) {
  if (!argv || !argv[0]) { fprintf(stderr, "[process] missing executable\n"); fflush(stderr); return NULL; }
  ui_process_t *process = calloc(1, sizeof(*process));
  if (!process) { fprintf(stderr, "[process] cannot allocate a child handle\n"); fflush(stderr); return NULL; }
  int error = posix_spawnp(&process->pid, argv[0], NULL, NULL, (char *const *)argv, environ);
  if (error) {
    fprintf(stderr, "[process] cannot start %s: %s\n", argv[0], strerror(error)); fflush(stderr);
    free(process);
    return NULL;
  }
  return process;
}

bool ui_process_poll(ui_process_t *process, int *exit_code) {
  if (!process->exited) {
    int status;
    pid_t pid = waitpid(process->pid, &status, WNOHANG);
    if (!pid || (pid < 0 && errno == EINTR)) return false;
    process->exited = true;
    process->status = pid > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  }
  if (exit_code) *exit_code = process->status;
  return true;
}

void ui_process_close(ui_process_t *process) {
  if (!process) return;
  if (!ui_process_poll(process, NULL)) {
    kill(process->pid, SIGKILL);
    while (waitpid(process->pid, NULL, 0) < 0 && errno == EINTR) {}
  }
  free(process);
}
#endif
