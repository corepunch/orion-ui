#include "remote_outbox.h"
#include <platform/platform.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
int remote_outbox_save_at(const char *b, const char *a, const char *o, const char *c, const char *u, const char *r) {
  (void)b; (void)a; (void)o; (void)c; (void)u; (void)r; return REMOTE_OUTBOX_ERROR;
}
int remote_outbox_load_at(const char *b, const char *a, const char *o, const char *c, const char *u, char **r) {
  (void)b; (void)a; (void)o; (void)c; (void)u; if (r) *r = NULL; return REMOTE_OUTBOX_ERROR;
}
int remote_outbox_clear_at(const char *b, const char *a, const char *o, const char *c, const char *u, const char *r) {
  (void)b; (void)a; (void)o; (void)c; (void)u; (void)r; return REMOTE_OUTBOX_ERROR;
}
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define OUTBOX_LIMIT (1024u * 1024u)
typedef struct { int directory, lock; char file[80]; char prefix[4096]; } outbox_t;

static bool_t identity_text(const char *s, size_t max, bool_t origin) {
  if (!s || !*s || strlen(s) > max) return FALSE;
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (origin) { if (*p < 33 || *p > 126 || *p == '"' || *p == '\\') return FALSE; }
    else if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
               (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.' || *p == ':')) return FALSE;
  }
  return TRUE;
}
static bool_t safe_file(int fd) {
  struct stat st;
  return fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
         !(st.st_mode & 077) && st.st_nlink == 1;
}
static int outbox_open(outbox_t *box, const char *base, const char *app, const char *origin,
                       const char *company, const char *actor) {
  *box = (outbox_t){.directory = -1, .lock = -1};
  if (!base || !*base || !identity_text(app, 100, FALSE) || !identity_text(origin, 2048, TRUE) ||
      !identity_text(company, 100, FALSE) || !identity_text(actor, 100, FALSE)) return REMOTE_OUTBOX_ERROR;
  int n = snprintf(box->prefix, sizeof(box->prefix),
                   "{\"version\":1,\"application\":\"%s\",\"origin\":\"%s\",\"company\":\"%s\",\"actor\":\"%s\",\"request\":\"",
                   app, origin, company, actor);
  if (n < 0 || (size_t)n >= sizeof(box->prefix)) return REMOTE_OUTBOX_ERROR;
  uint64_t hash1 = UINT64_C(14695981039346656037), hash2 = UINT64_C(1099511628211);
  for (const unsigned char *p = (const unsigned char *)box->prefix; *p; p++) {
    hash1 = (hash1 ^ *p) * UINT64_C(1099511628211);
    hash2 = (hash2 ^ *p) * UINT64_C(14029467366897019727);
  }
  char directory[2048], lock[90];
  n = snprintf(directory, sizeof(directory), "%s/orion-remote-outbox", base);
  if (n < 0 || (size_t)n >= sizeof(directory)) return REMOTE_OUTBOX_ERROR;
  if (mkdir(directory, 0700) && errno != EEXIST) return REMOTE_OUTBOX_ERROR;
  int parent = open(base, O_RDONLY | O_DIRECTORY);
  if (parent < 0) return REMOTE_OUTBOX_ERROR;
  bool_t synced = fsync(parent) == 0; close(parent);
  if (!synced) return REMOTE_OUTBOX_ERROR;
  box->directory = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  if (box->directory < 0) return REMOTE_OUTBOX_ERROR;
  struct stat st;
  if (fstat(box->directory, &st) || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 077)) goto failed;
  snprintf(box->file, sizeof(box->file), "%016llx%016llx.json", (unsigned long long)hash1, (unsigned long long)hash2);
  snprintf(lock, sizeof(lock), "%s.lock", box->file);
  box->lock = openat(box->directory, lock, O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
  if (box->lock < 0 || !safe_file(box->lock) || flock(box->lock, LOCK_EX | LOCK_NB)) goto failed;
  return REMOTE_OUTBOX_OK;
failed:
  if (box->lock >= 0) close(box->lock);
  close(box->directory); box->lock = box->directory = -1;
  return REMOTE_OUTBOX_ERROR;
}
static void outbox_close(outbox_t *box) {
  if (box->lock >= 0) close(box->lock);
  if (box->directory >= 0) close(box->directory);
}
static int outbox_read(outbox_t *box, char **request) {
  *request = NULL;
  int fd = openat(box->directory, box->file, O_RDONLY | O_NOFOLLOW);
  if (fd < 0) return errno == ENOENT ? REMOTE_OUTBOX_EMPTY : REMOTE_OUTBOX_ERROR;
  struct stat st;
  if (!safe_file(fd) || fstat(fd, &st) || st.st_size <= 0 || st.st_size > OUTBOX_LIMIT) { close(fd); return REMOTE_OUTBOX_ERROR; }
  size_t size = (size_t)st.st_size, used = 0;
  char *data = malloc(size + 1);
  if (!data) { close(fd); return REMOTE_OUTBOX_ERROR; }
  while (used < size) {
    ssize_t n = read(fd, data + used, size - used);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { free(data); close(fd); return REMOTE_OUTBOX_ERROR; }
    used += (size_t)n;
  }
  close(fd); data[size] = 0;
  size_t prefix = strlen(box->prefix);
  if (size < prefix + 2 || memcmp(data, box->prefix, prefix) || data[size - 2] != '"' || data[size - 1] != '}') {
    free(data); return REMOTE_OUTBOX_CONFLICT;
  }
  char *decoded = malloc(size + 1); size_t at = 0;
  if (!decoded) { free(data); return REMOTE_OUTBOX_ERROR; }
  for (size_t i = prefix; i < size - 2; i++) {
    unsigned char c = (unsigned char)data[i];
    if (c == '\\') {
      if (++i >= size - 2) { free(data); free(decoded); return REMOTE_OUTBOX_ERROR; }
      c = (unsigned char)data[i];
      if (c == '"' || c == '\\') decoded[at++] = (char)c;
      else { free(data); free(decoded); return REMOTE_OUTBOX_ERROR; }
    } else if (c < 32 || c == '"') { free(data); free(decoded); return REMOTE_OUTBOX_ERROR; }
    else decoded[at++] = (char)c;
  }
  decoded[at] = 0; free(data); *request = decoded; return REMOTE_OUTBOX_OK;
}
static bool_t valid_utf8(const unsigned char *p) {
  while (*p) {
    if (*p < 0x80) { p++; continue; }
    unsigned count, value, minimum;
    if (*p >= 0xc2 && *p <= 0xdf) { count = 1; value = *p & 0x1f; minimum = 0x80; }
    else if (*p >= 0xe0 && *p <= 0xef) { count = 2; value = *p & 0x0f; minimum = 0x800; }
    else if (*p >= 0xf0 && *p <= 0xf4) { count = 3; value = *p & 0x07; minimum = 0x10000; }
    else return FALSE;
    p++;
    for (unsigned i = 0; i < count; i++) {
      if (!*p || (*p & 0xc0) != 0x80) return FALSE;
      value = (value << 6) | (*p++ & 0x3f);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return FALSE;
  }
  return TRUE;
}

static int outbox_encode(outbox_t *box, const char *request, char **envelope, size_t *size) {
  if (!request || !*request || strlen(request) > OUTBOX_LIMIT || !valid_utf8((const unsigned char *)request)) return REMOTE_OUTBOX_ERROR;
  size_t prefix = strlen(box->prefix), length = strlen(request), used = prefix;
  if (prefix + 2 > OUTBOX_LIMIT) return REMOTE_OUTBOX_ERROR;
  char *data = malloc(OUTBOX_LIMIT + 1);
  if (!data) return REMOTE_OUTBOX_ERROR;
  memcpy(data, box->prefix, prefix);
  for (size_t i = 0; i < length; i++) {
    unsigned char c = (unsigned char)request[i];
    if (c < 32 || c == 127) { free(data); return REMOTE_OUTBOX_ERROR; }
    if (c == '"' || c == '\\') { if (used + 3 > OUTBOX_LIMIT) { free(data); return REMOTE_OUTBOX_ERROR; } data[used++] = '\\'; }
    if (used + 3 > OUTBOX_LIMIT) { free(data); return REMOTE_OUTBOX_ERROR; }
    data[used++] = (char)c;
  }
  data[used++] = '"'; data[used++] = '}'; data[used] = 0;
  *envelope = data; *size = used; return REMOTE_OUTBOX_OK;
}
int remote_outbox_load_at(const char *base, const char *app, const char *origin, const char *company, const char *actor, char **request) {
  if (!request) return REMOTE_OUTBOX_ERROR;
  *request = NULL; outbox_t box;
  int result = outbox_open(&box, base, app, origin, company, actor);
  if (result == REMOTE_OUTBOX_OK) { result = outbox_read(&box, request); outbox_close(&box); }
  if (result == REMOTE_OUTBOX_ERROR || result == REMOTE_OUTBOX_CONFLICT) { fprintf(stderr, "[remote_outbox] cannot load pending request status=%d\n", result); fflush(stderr); }
  return result;
}
int remote_outbox_save_at(const char *base, const char *app, const char *origin, const char *company, const char *actor, const char *request) {
  outbox_t box; int result = outbox_open(&box, base, app, origin, company, actor);
  if (result != REMOTE_OUTBOX_OK) goto report;
  char *old = NULL, *data = NULL; size_t size = 0;
  result = outbox_read(&box, &old);
  if (result == REMOTE_OUTBOX_OK) {
    result = request && !strcmp(old, request) ? (fsync(box.directory) == 0 ? REMOTE_OUTBOX_OK : REMOTE_OUTBOX_ERROR) : REMOTE_OUTBOX_CONFLICT;
    free(old); goto done;
  }
  if (result != REMOTE_OUTBOX_EMPTY) goto done;
  result = outbox_encode(&box, request, &data, &size);
  if (result != REMOTE_OUTBOX_OK) goto done;
  char temporary[128]; int fd = -1;
  for (unsigned i = 0; i < 256; i++) {
    snprintf(temporary, sizeof(temporary), ".%s.%ld.%u.tmp", box.file, (long)getpid(), i);
    fd = openat(box.directory, temporary, O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
    if (fd >= 0 || errno != EEXIST) break;
  }
  if (fd < 0) { result = REMOTE_OUTBOX_ERROR; goto done; }
  size_t written = 0;
  while (written < size) {
    ssize_t n = write(fd, data + written, size - written);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    written += (size_t)n;
  }
  bool_t ok = written == size && fsync(fd) == 0;
  if (close(fd)) ok = FALSE;
  if (ok && renameat(box.directory, temporary, box.directory, box.file)) ok = FALSE;
  if (!ok) unlinkat(box.directory, temporary, 0);
  if (ok && fsync(box.directory)) ok = FALSE;
  result = ok ? REMOTE_OUTBOX_OK : REMOTE_OUTBOX_ERROR;
done:
  free(data); outbox_close(&box);
report:
  if (result == REMOTE_OUTBOX_ERROR || result == REMOTE_OUTBOX_CONFLICT) { fprintf(stderr, "[remote_outbox] cannot save pending request status=%d\n", result); fflush(stderr); }
  return result;
}
int remote_outbox_clear_at(const char *base, const char *app, const char *origin, const char *company, const char *actor, const char *expected) {
  outbox_t box; int result = outbox_open(&box, base, app, origin, company, actor);
  if (result != REMOTE_OUTBOX_OK) goto report;
  char *old = NULL; result = outbox_read(&box, &old);
  if (result == REMOTE_OUTBOX_OK) {
    if (!expected || strcmp(old, expected)) result = REMOTE_OUTBOX_CONFLICT;
    else if (unlinkat(box.directory, box.file, 0) || fsync(box.directory)) result = REMOTE_OUTBOX_ERROR;
    free(old);
  }
  outbox_close(&box);
report:
  if (result == REMOTE_OUTBOX_ERROR || result == REMOTE_OUTBOX_CONFLICT) { fprintf(stderr, "[remote_outbox] cannot clear pending request status=%d\n", result); fflush(stderr); }
  return result;
}
#endif
static const char *remote_outbox_base(char *buffer, size_t size) {
  const char *override = getenv("ORION_REMOTE_PROFILE");
  if (!override || !*override) return axSettingsDirectory();
#ifdef _WIN32
  (void)buffer; (void)size;
  return NULL;
#else
  struct stat st;
  if (lstat(override, &st) || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0777) != 0700) {
    fprintf(stderr, "[remote_outbox] invalid private profile directory\n"); fflush(stderr); return NULL;
  }
  char *resolved = realpath(override, NULL);
  struct stat actual;
  if (!resolved || strlen(resolved) >= size || stat(resolved, &actual) || actual.st_dev != st.st_dev || actual.st_ino != st.st_ino) {
    free(resolved); fprintf(stderr, "[remote_outbox] cannot resolve private profile directory\n"); fflush(stderr); return NULL;
  }
  snprintf(buffer, size, "%s", resolved); free(resolved); return buffer;
#endif
}
int remote_outbox_save(const char *a, const char *o, const char *c, const char *u, const char *r) {
  char base[4096]; return remote_outbox_save_at(remote_outbox_base(base, sizeof(base)), a, o, c, u, r);
}
int remote_outbox_load(const char *a, const char *o, const char *c, const char *u, char **r) {
  char base[4096]; return remote_outbox_load_at(remote_outbox_base(base, sizeof(base)), a, o, c, u, r);
}
int remote_outbox_clear(const char *a, const char *o, const char *c, const char *u, const char *r) {
  char base[4096]; return remote_outbox_clear_at(remote_outbox_base(base, sizeof(base)), a, o, c, u, r);
}
