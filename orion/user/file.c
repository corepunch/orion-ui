#include "file.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static wchar_t *wide_path(const char *path) {
  int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
  if (!count) return NULL;
  wchar_t *wide = malloc((size_t)count * sizeof(*wide));
  if (wide && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, count)) { free(wide); return NULL; }
  return wide;
}
#endif

bool file_replace(const char *source, const char *destination) {
  if (!source || !destination) { fprintf(stderr, "[file] replacement requires source and destination paths\n"); return false; }
#ifdef _WIN32
  wchar_t *from = wide_path(source), *to = wide_path(destination);
  bool ok = from && to && MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
  DWORD error = ok ? 0 : GetLastError();
  free(from); free(to);
  if (!ok) fprintf(stderr, "[file] cannot replace %s: Windows error %lu\n", destination, (unsigned long)error);
#else
  bool ok = rename(source, destination) == 0;
  if (!ok) { fprintf(stderr, "[file] cannot replace %s: ", destination); perror(NULL); }
#endif
  return ok;
}
