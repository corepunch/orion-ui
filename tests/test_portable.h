// POSIX calls the tests rely on, for the Windows (MinGW) build.
#ifndef __TEST_PORTABLE_H__
#define __TEST_PORTABLE_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_NULL_DEVICE "NUL"
static inline int test_mkdir(const char *path, int mode) { (void)mode; return _mkdir(path); }

// Replaces the trailing "XXXXXX" (before `suffix_len` characters of suffix) and creates the entry.
static inline int test_make_temp(char *tmpl, int suffix_len, int dir) {
  static const char k_chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  size_t len = strlen(tmpl);
  if (len < 6 + (size_t)suffix_len) return -1;
  char *x = tmpl + len - 6 - suffix_len;
  if (!strncmp(tmpl, "/tmp/", 5)) _mkdir("/tmp");  // Windows has no /tmp; tests name their scratch files there
  for (int tries = 0; tries < 100; tries++) {
    for (int i = 0; i < 6; i++) x[i] = k_chars[rand() % (int)(sizeof(k_chars) - 1)];
    if (dir) { if (_mkdir(tmpl) == 0) return 0; }
    else { int fd = _open(tmpl, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE); if (fd >= 0) return fd; }
  }
  return -1;
}
static inline char *mkdtemp(char *tmpl)                  { return test_make_temp(tmpl, 0, 1) == 0 ? tmpl : NULL; }
static inline int   mkstemps(char *tmpl, int suffix_len) { return test_make_temp(tmpl, suffix_len, 0); }
static inline int   setenv(const char *name, const char *value, int overwrite) {
  if (!overwrite && getenv(name)) return 0;
  return _putenv_s(name, value);
}
#else
#include <sys/stat.h>
#include <unistd.h>
#define TEST_NULL_DEVICE "/dev/null"
static inline int test_mkdir(const char *path, int mode) { return mkdir(path, (mode_t)mode); }
#endif

// Sends stderr to the null device; test_stderr_restore() undoes it. Assigning to stderr is not portable.
static inline int test_stderr_silence(void) {
  fflush(stderr);
  int saved = dup(2);
  if (!freopen(TEST_NULL_DEVICE, "w", stderr)) return -1;
  return saved;
}
static inline void test_stderr_restore(int saved) {
  if (saved < 0) return;
  fflush(stderr);
  dup2(saved, 2);
  close(saved);
  clearerr(stderr);
}

#endif
