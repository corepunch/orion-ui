#include "media_export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

bool ui_media_video_frame(const ui_media_video_t *v, int64_t index, uint8_t *rgba) {
  if (!v || !v->rgba || !rgba || v->width <= 0 || v->height <= 0 || v->width > 4096 || v->height > 4096 ||
      v->x < 0 || v->y < 0 || v->w < 1 || v->h < 1 || v->x + v->w > v->width || v->y + v->h > v->height ||
      v->frames < 1 || v->rate < 1 || v->fps < 1 || index < 0 || index >= (v->frames * v->fps + v->rate - 1) / v->rate) {
    fprintf(stderr, "[media] invalid video frame request\n"); fflush(stderr); return false;
  }
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
extern char **environ;
#endif

// No shell: document paths are separate arguments, including spaces and quotes.
static bool media_run(char *const argv[], ui_media_progress_fn progress, void *userdata) {
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
  if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &start, &proc)) return false;
  bool cancelled = false;
  while (WaitForSingleObject(proc.hProcess, 20) == WAIT_TIMEOUT) {
    if (progress && !progress(userdata, 0.5f)) { TerminateProcess(proc.hProcess, 1); cancelled = true; break; }
  }
  WaitForSingleObject(proc.hProcess, INFINITE);
  DWORD status = 1; GetExitCodeProcess(proc.hProcess, &status);
  CloseHandle(proc.hThread); CloseHandle(proc.hProcess);
  return !cancelled && status == 0;
#else
  pid_t pid;
  int status = 0;
  if (posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ)) return false;
  for (;;) {
    pid_t done = waitpid(pid, &status, WNOHANG);
    if (done == pid) return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (done < 0) return false;
    if (progress && !progress(userdata, 0.5f)) { kill(pid, SIGTERM); waitpid(pid, &status, 0); return false; }
    axSleep(20);
  }
#endif
}

bool ui_media_encode(const char *wav, const char *output, ui_media_format_t format,
                     const ui_media_video_t *v, ui_media_progress_fn progress, void *userdata) {
  bool ok = false;
  char *audio[] = {"ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-n", "-i", (char *)wav,
    "-vn", "-c:a", "aac", "-b:a", "192k", "-f", "ipod", (char *)output, NULL};
  if (format == UI_MEDIA_M4A) ok = media_run(audio, progress, userdata);
  else if (format == UI_MEDIA_MP4 && v) {
    char raw[1200], size[48], fps[24], duration[48], filter[512], color[80];
    snprintf(raw, sizeof(raw), "%s.rgba", output);
    snprintf(size, sizeof(size), "%dx%d", v->width, v->height);
    snprintf(fps, sizeof(fps), "%d", v->fps);
    snprintf(duration, sizeof(duration), "%lldus", (long long)(v->frames * 1000000 / v->rate));
    snprintf(color, sizeof(color), "color=c=0x%02x%02x%02x:s=4x%d:r=%d", (unsigned)(v->color & 255),
      (unsigned)((v->color >> 8) & 255), (unsigned)((v->color >> 16) & 255), v->h, v->fps);
    snprintf(filter, sizeof(filter), "[0:v][2:v]overlay=x='%d+min(%d,t*%d*%d/%lld)':y=%d:shortest=1[v]",
      v->x, v->w - 1, v->rate, v->w - 1, (long long)v->frames, v->y);
    FILE *f = fopen(raw, "wb");
    size_t bytes = (size_t)v->width * v->height * 4;
    ok = f && fwrite(v->rgba, 1, bytes, f) == bytes;
    if (f && fclose(f)) ok = false;
    char *args[] = {"ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-n", "-stream_loop", "-1", "-f", "rawvideo", "-pixel_format", "rgba",
      "-video_size", size, "-framerate", fps, "-i", raw, "-i", (char *)wav, "-f", "lavfi", "-i", color,
      "-filter_complex", filter, "-map", "[v]", "-map", "1:a", "-t", duration,
      "-c:v", "libx264", "-preset", "fast", "-crf", "20", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k",
      "-movflags", "+faststart", "-f", "mp4", (char *)output, NULL};
    if (ok) ok = media_run(args, progress, userdata);
    remove(raw);
  }
  if (!ok) { remove(output); fprintf(stderr, "[media] encoding failed or cancelled; desktop AAC/MP4 requires ffmpeg\n"); fflush(stderr); }
  return ok;
}

bool ui_media_share(const char *path) { (void)path; return false; }
#endif
