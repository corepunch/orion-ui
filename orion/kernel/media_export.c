#include "media_export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

bool ui_media_video_frame(const ui_media_video_t *v, int64_t index, uint8_t *rgba) {
  if (!v || !rgba || v->width <= 0 || v->height <= 0 || v->width > 4096 || v->height > 4096 ||
      v->frames < 1 || v->rate < 1 || v->fps < 1 || index < 0 || index >= (v->frames * v->fps + v->rate - 1) / v->rate ||
      (!v->render && (!v->rgba || v->x < 0 || v->y < 0 || v->w < 1 || v->h < 1 || v->x + v->w > v->width || v->y + v->h > v->height))) {
    fprintf(stderr, "[media] invalid video frame request index=%lld\n", (long long)index); fflush(stderr); return false;
  }
  if (v->render) return v->render(v->userdata, index, rgba);
  memcpy(rgba, v->rgba, (size_t)v->width * v->height * 4);
  int x = v->x + (int)((double)index * v->rate * (v->w - 1) / (v->frames * (double)v->fps));
  for (int y = v->y; y < v->y + v->h; y++) for (int dx = 0; dx < 3 && x + dx < v->x + v->w; dx++) {
    uint8_t *p = rgba + ((size_t)y * v->width + x + dx) * 4;
    p[0] = (uint8_t)v->color; p[1] = (uint8_t)(v->color >> 8); p[2] = (uint8_t)(v->color >> 16); p[3] = 255;
  }
  return true;
}

#ifndef __APPLE__
#include <platform/platform.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
extern char **environ;
#endif

#ifdef _WIN32
typedef HANDLE media_pipe_t;
static bool media_pipe_write(media_pipe_t p, const uint8_t *data, size_t bytes) {
  while (bytes) {
    DWORD chunk = bytes > (1u << 30) ? (1u << 30) : (DWORD)bytes, wrote = 0;
    if (!WriteFile(p, data, chunk, &wrote, NULL) || !wrote) return false;
    data += wrote; bytes -= wrote;
  }
  return true;
}
#else
typedef int media_pipe_t;
static bool media_pipe_write(media_pipe_t p, const uint8_t *data, size_t bytes) {
  while (bytes) {
    ssize_t wrote = write(p, data, bytes);
    if (wrote < 0 && errno == EINTR) continue;
    if (wrote <= 0) return false;
    data += wrote; bytes -= (size_t)wrote;
  }
  return true;
}
#endif

// Renders every frame into the encoder's stdin; nothing is spooled to disk.
// Returns false on cancellation (*cancelled set) or a closed pipe.
static bool media_feed(media_pipe_t p, const ui_media_video_t *v, ui_media_progress_fn progress, void *userdata, bool *cancelled) {
  size_t bytes = (size_t)v->width * v->height * 4;
  uint8_t *rgba = malloc(bytes);
  if (!rgba) { fprintf(stderr, "[media] frame allocation failed size=%dx%d\n", v->width, v->height); fflush(stderr); return false; }
  int64_t count = (v->frames * v->fps + v->rate - 1) / v->rate;
  bool ok = true;
  for (int64_t i = 0; ok && i < count; i++) {
    if (progress && !progress(userdata, (float)i / count * .95f)) { *cancelled = true; ok = false; break; }
    ok = ui_media_video_frame(v, i, rgba) && media_pipe_write(p, rgba, bytes);
  }
  free(rgba);
  return ok;
}

// No shell: document paths are separate arguments, including spaces and quotes.
// With a video, frames are piped to the child's stdin.
static bool media_run(char *const argv[], const ui_media_video_t *video, ui_media_progress_fn progress, void *userdata) {
#ifdef _WIN32
  char command[8192] = {0}; size_t used = 0;
  for (int i = 0; argv[i]; i++) {
    if (used + strlen(argv[i]) * 2 + 8 >= sizeof(command)) return false;
    command[used++] = '"';
    const char *p = argv[i];
    while (*p) {
      int slashes = 0;
      while (*p == '\\') { slashes++; p++; }
      int count = (*p == '"' || !*p) ? slashes * 2 : slashes;
      while (count--) command[used++] = '\\';
      if (!*p) break;
      if (*p == '"') command[used++] = '\\';
      command[used++] = *p++;
    }
    command[used++] = '"'; command[used++] = ' '; command[used] = 0;
  }
  STARTUPINFOA start = {sizeof(start)}; PROCESS_INFORMATION proc = {0};
  HANDLE read_end = NULL, write_end = NULL;
  SECURITY_ATTRIBUTES inherit = {sizeof(inherit), NULL, TRUE};
  if (video) {
    if (!CreatePipe(&read_end, &write_end, &inherit, 0)) return false;
    SetHandleInformation(write_end, HANDLE_FLAG_INHERIT, 0);
    start.dwFlags = STARTF_USESTDHANDLES; start.hStdInput = read_end;
    start.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE); start.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  }
  bool started = CreateProcessA(NULL, command, NULL, NULL, video != NULL, CREATE_NO_WINDOW, NULL, NULL, &start, &proc);
  if (read_end) CloseHandle(read_end);
  if (!started) { if (write_end) CloseHandle(write_end); return false; }
  bool cancelled = false, fed = true;
  if (video) {
    fed = media_feed(write_end, video, progress, userdata, &cancelled);
    CloseHandle(write_end);
    if (cancelled) TerminateProcess(proc.hProcess, 1);
  }
  while (!cancelled && WaitForSingleObject(proc.hProcess, 20) == WAIT_TIMEOUT) {
    if (progress && !progress(userdata, video ? .97f : .5f)) { TerminateProcess(proc.hProcess, 1); cancelled = true; break; }
  }
  WaitForSingleObject(proc.hProcess, INFINITE);
  DWORD status = 1; GetExitCodeProcess(proc.hProcess, &status);
  CloseHandle(proc.hThread); CloseHandle(proc.hProcess);
  return !cancelled && fed && status == 0;
#else
  pid_t pid;
  int status = 0, fds[2] = {-1, -1};
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  if (video) {
    if (pipe(fds)) { posix_spawn_file_actions_destroy(&actions); return false; }
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_adddup2(&actions, fds[0], 0);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
  }
  bool started = posix_spawnp(&pid, argv[0], &actions, NULL, argv, environ) == 0;
  posix_spawn_file_actions_destroy(&actions);
  if (fds[0] >= 0) close(fds[0]);
  if (!started) { if (fds[1] >= 0) close(fds[1]); return false; }
  if (video) {
    // A dying encoder must surface as EPIPE on this worker, not kill the app.
    sigset_t pipe_signal, previous;
    sigemptyset(&pipe_signal); sigaddset(&pipe_signal, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &pipe_signal, &previous);
    bool cancelled = false, fed = media_feed(fds[1], video, progress, userdata, &cancelled);
    close(fds[1]);
    struct timespec now = {0, 0};
    while (sigtimedwait(&pipe_signal, NULL, &now) == SIGPIPE) {}
    pthread_sigmask(SIG_SETMASK, &previous, NULL);
    if (cancelled) { kill(pid, SIGTERM); waitpid(pid, &status, 0); return false; }
    if (!fed) { waitpid(pid, &status, 0); return false; }
  }
  for (;;) {
    pid_t done = waitpid(pid, &status, WNOHANG);
    if (done == pid) return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (done < 0) return false;
    if (progress && !progress(userdata, video ? .97f : .5f)) { kill(pid, SIGTERM); waitpid(pid, &status, 0); return false; }
    axSleep(20);
  }
#endif
}

bool ui_media_encode(const char *wav, const char *output, ui_media_format_t format,
                     const ui_media_video_t *v, ui_media_progress_fn progress, void *userdata) {
  bool ok = false;
  char *audio[] = {"ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-n", "-i", (char *)wav,
    "-vn", "-c:a", "aac", "-b:a", "192k", "-f", "ipod", (char *)output, NULL};
  if (format == UI_MEDIA_M4A) ok = media_run(audio, NULL, progress, userdata);
  else if (format == UI_MEDIA_MP4 && v) {
    char size[48], fps[24], duration[48];
    snprintf(size, sizeof(size), "%dx%d", v->width, v->height);
    snprintf(fps, sizeof(fps), "%d", v->fps);
    snprintf(duration, sizeof(duration), "%lldus", (long long)(v->frames * 1000000 / v->rate));
    char *args[] = {"ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-n", "-f", "rawvideo", "-pixel_format", "rgba",
      "-video_size", size, "-framerate", fps, "-i", "pipe:0", "-i", (char *)wav,
      "-map", "0:v", "-map", "1:a", "-t", duration,
      "-c:v", "libx264", "-preset", "fast", "-crf", "18", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k",
      "-movflags", "+faststart", "-f", "mp4", (char *)output, NULL};
    ok = media_run(args, v, progress, userdata);
  }
  if (!ok) { remove(output); fprintf(stderr, "[media] encoding failed or cancelled; desktop AAC/MP4 requires ffmpeg\n"); fflush(stderr); }
  return ok;
}

bool ui_media_share(const char *path) { (void)path; return false; }
bool ui_media_share_url(const char *url) { (void)url; return false; }
#endif
