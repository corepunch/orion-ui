// Git backend — thin popen-based subprocess wrapper.
//
// Every public function that needs to run git changes into repo->path first,
// using popen() with the command prefixed by "cd <path> && ".
// Async operations fire a detached POSIX/Win32 thread and post evGitOpDone
// back to the caller's window via post_message(), mirroring the pattern used
// by kernel/http.c.
//
// TODO(platform): The three pieces of OS-level functionality used here should
// eventually be provided by the Orion platform layer (platform/platform.h) so
// that applications do not need conditional compilation or raw POSIX/Win32
// calls.  Specifically:
//
//   (A) Thread creation / detach — equivalent to axThread(fn, arg) /
//       axThreadDetach(t) — mirrors kernel/http.c's internal thread helpers.
//       Once the platform exposes axThread*, remove the #ifdef _WIN32 block
//       under "Cross-platform thread helpers" below.
//
//   (B) Subprocess execution (popen + output capture + exit code) —
//       equivalent to axRunCommand(cmd, out_buf, out_sz) → int exit_code.
//       Once the platform exposes axRunCommand, replace gc_popen_read() with
//       a thin wrapper and remove the gc_build_cmd / gc_popen_read helpers.
//
//   (C) Async subprocess — equivalent to axRunCommandAsync(cmd, op,
//       notify_win, post_msg) — mirrors http_request_async().  Once the
//       platform exposes this, git_run_async() reduces to a single call.

#include "gitclient.h"

#ifdef _WIN32
#  include <windows.h>
#else
#  include <pthread.h>
#endif

// ============================================================
// Internal repository type
// ============================================================

struct git_repo_s {
  char path[512];   // absolute path to the working tree
};

// ============================================================
// Cross-platform thread helpers
// TODO(platform-A): replace with axThread* once the platform layer exposes
// a portable thread-creation / detach API (see file-level TODO above).
// ============================================================

#ifdef _WIN32
typedef HANDLE git_thread_t;
#define GIT_THREAD_RET DWORD WINAPI
static bool git_thread_create(git_thread_t *t,
                               GIT_THREAD_RET (*fn)(void *), void *arg) {
  *t = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)fn, arg, 0, NULL);
  return *t != NULL;
}
static void git_thread_detach(git_thread_t t) { CloseHandle(t); }
#else
typedef pthread_t git_thread_t;
#define GIT_THREAD_RET void *
static bool git_thread_create(git_thread_t *t,
                               GIT_THREAD_RET (*fn)(void *), void *arg) {
  if (pthread_create(t, NULL, fn, arg) != 0) return false;
  pthread_detach(*t);
  return true;
}
static void git_thread_detach(git_thread_t t) { (void)t; /* already detached */ }
#endif

// ============================================================
// Low-level helpers
// TODO(platform-B): gc_build_cmd + gc_popen_read should be replaced by
// axRunCommand(cmd, buf, buf_sz) → int exit_code once the platform layer
// provides a portable subprocess API (see file-level TODO above).
// ============================================================

// Build "cd <path> && git <args…>" into buf.
static void gc_build_cmd(const char *path, const char *args[],
                         char *buf, int buf_sz) {
  int n = snprintf(buf, (size_t)buf_sz, "cd \"%s\" && git", path);
  for (int i = 1; args[i] && n < buf_sz - 2; i++) {
    n += snprintf(buf + n, (size_t)(buf_sz - n), " %s", args[i]);
  }
  // Redirect stderr to stdout so callers capture error text too.
  snprintf(buf + n, (size_t)(buf_sz - n), " 2>&1");
}

// Run cmd via popen(), read all output into buf (up to buf_sz-1 bytes).
// Returns the exit code (0 = success).
static int gc_popen_read(const char *cmd, char *buf, int buf_sz) {
  if (!cmd || !buf || buf_sz <= 0) return -1;

  FILE *fp = popen(cmd, "r");
  if (!fp) { buf[0] = '\0'; return -1; }

  int total = 0;
  char tmp[256];
  while (fgets(tmp, sizeof(tmp), fp)) {
    int len = (int)strlen(tmp);
    if (total + len < buf_sz - 1) {
      memcpy(buf + total, tmp, (size_t)len);
      total += len;
    }
  }
  buf[total] = '\0';

  int rc = pclose(fp);
#ifdef _WIN32
  return rc;
#else
  return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#endif
}

// ============================================================
// Public: open / close repository
// ============================================================

git_repo_t *git_repo_open(const char *path) {
  if (!path || !path[0]) return NULL;

  // Quick sanity: check .git directory or file exists.
  char check[640];
  snprintf(check, sizeof(check), "%s/.git", path);

  // Use access() on POSIX; on Win32, use PathFileExists or just try.
#ifndef _WIN32
  {
#include <sys/stat.h>
    struct stat st;
    if (stat(check, &st) != 0) {
      GC_LOG("git_repo_open: no .git at %s", path);
      return NULL;
    }
  }
#endif

  git_repo_t *repo = (git_repo_t *)calloc(1, sizeof(git_repo_t));
  if (!repo) return NULL;
  strncpy(repo->path, path, sizeof(repo->path) - 1);
  GC_LOG("git_repo_open: opened %s", repo->path);
  return repo;
}

void git_repo_close(git_repo_t *repo) {
  if (repo) free(repo);
}

bool git_repo_valid(git_repo_t *repo) {
  return repo && repo->path[0] != '\0';
}

const char *git_repo_path(git_repo_t *repo) {
  return repo ? repo->path : "";
}

// ============================================================
// Public: run a git command synchronously
// ============================================================

bool git_run_sync(git_repo_t *repo, const char *args[],
                  char *buf, int buf_sz) {
  if (!repo || !args) return false;
  char cmd[2048];
  gc_build_cmd(repo->path, args, cmd, sizeof(cmd));
  GC_LOG("git_run_sync: %s", cmd);
  int rc = gc_popen_read(cmd, buf, buf_sz);
  return rc == 0;
}

// ============================================================
// Public: git log
// ============================================================

int git_get_log(git_repo_t *repo, git_commit_t *out, int max) {
  if (!repo || !out || max <= 0) return 0;

  // Format: hash<US>author<US>date<US>subject<RS>
  char cmd[1024];
  snprintf(cmd, sizeof(cmd),
    "cd \"%s\" && git log --max-count=%d "
    "--format=\"%%H\x1f%%an\x1f%%ad\x1f%%s\x1e\" "
    "--date=format:\"%%Y-%%m-%%d %%H:%%M\" 2>/dev/null",
    repo->path, max);

  char buf[64 * 1024];
  gc_popen_read(cmd, buf, sizeof(buf));

  int count = 0;
  char *p = buf;
  while (*p && count < max) {
    git_commit_t *c = &out[count];
    char *end;

    // hash (40 chars)
    end = strchr(p, '\x1f');
    if (!end) break;
    int n = (int)(end - p);
    if (n > 40) n = 40;
    memcpy(c->hash, p, (size_t)n);
    c->hash[n] = '\0';
    p = end + 1;

    // author
    end = strchr(p, '\x1f');
    if (!end) break;
    n = (int)(end - p);
    if (n >= 64) n = 63;
    memcpy(c->author, p, (size_t)n);
    c->author[n] = '\0';
    p = end + 1;

    // date
    end = strchr(p, '\x1f');
    if (!end) break;
    n = (int)(end - p);
    if (n >= 20) n = 19;
    memcpy(c->date, p, (size_t)n);
    c->date[n] = '\0';
    p = end + 1;

    // subject
    end = strchr(p, '\x1e');
    if (!end) {
      // last record may not have delimiter
      strncpy(c->subject, p, sizeof(c->subject) - 1);
      c->subject[sizeof(c->subject) - 1] = '\0';
      count++;
      break;
    }
    n = (int)(end - p);
    if (n >= 256) n = 255;
    memcpy(c->subject, p, (size_t)n);
    c->subject[n] = '\0';
    p = end + 1;
    // skip \n after record separator
    if (*p == '\n') p++;

    count++;
  }
  GC_LOG("git_get_log: %d commits", count);
  return count;
}

// ============================================================
// Public: git status
// ============================================================

int git_get_status(git_repo_t *repo, git_file_status_t *out, int max) {
  if (!repo || !out || max <= 0) return 0;

  char buf[32 * 1024];
  const char *args[] = { "git", "status", "--porcelain", "-u", NULL };
  git_run_sync(repo, args, buf, sizeof(buf));

  int count = 0;
  char *line = buf;
  while (*line && count < max) {
    char *nl = strchr(line, '\n');
    if (!nl) break;
    *nl = '\0';

    if (strlen(line) < 4) { line = nl + 1; continue; }

    git_file_status_t *f = &out[count];
    char xy_x = line[0];
    char xy_y = line[1];
    // XY format: index=X, worktree=Y
    if (xy_x != ' ' && xy_x != '?') {
      f->status = xy_x;
      f->staged = true;
      strncpy(f->path, line + 3, sizeof(f->path) - 1);
      f->path[sizeof(f->path) - 1] = '\0';
      count++;
    }
    if (xy_y != ' ' && xy_y != '?' && count < max) {
      if (xy_x == ' ' || xy_x == '?') {
        f = &out[count];
        f->status = xy_y;
        f->staged = false;
        strncpy(f->path, line + 3, sizeof(f->path) - 1);
        f->path[sizeof(f->path) - 1] = '\0';
        count++;
      }
    } else if (xy_x == '?' && xy_y == '?') {
      f->status = '?';
      f->staged = false;
      strncpy(f->path, line + 3, sizeof(f->path) - 1);
      f->path[sizeof(f->path) - 1] = '\0';
      count++;
    }

    line = nl + 1;
  }
  GC_LOG("git_get_status: %d files", count);
  return count;
}

// ============================================================
// Public: git diff
// ============================================================

bool git_get_diff(git_repo_t *repo, const char *path,
                  bool staged, char *buf, int buf_sz) {
  if (!repo || !buf || buf_sz <= 0) return false;

  char cmd[2048];
  if (staged) {
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && git diff --cached -- \"%s\" 2>/dev/null",
             repo->path, path ? path : "");
  } else if (path && path[0]) {
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && git diff HEAD -- \"%s\" 2>/dev/null",
             repo->path, path);
  } else {
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && git diff HEAD 2>/dev/null",
             repo->path);
  }

  gc_popen_read(cmd, buf, buf_sz);
  return true;
}

// ============================================================
// Public: branches
// ============================================================

int git_get_branches(git_repo_t *repo, git_branch_t *out, int max) {
  if (!repo || !out || max <= 0) return 0;

  char buf[16 * 1024];
  const char *args[] = { "git", "branch", "-a", "--no-color", NULL };
  git_run_sync(repo, args, buf, sizeof(buf));

  int count = 0;
  char *line = buf;
  while (*line && count < max) {
    char *nl = strchr(line, '\n');
    if (!nl) break;
    *nl = '\0';

    if (strlen(line) < 2) { line = nl + 1; continue; }

    git_branch_t *b = &out[count];
    b->is_current = (line[0] == '*');
    b->is_remote  = false;

    const char *name = line + 2;  // skip "* " or "  "
    if (strncmp(name, "remotes/", 8) == 0) {
      name += 8;
      b->is_remote = true;
    }
    // Strip trailing whitespace / tracking info (e.g. " -> origin/HEAD")
    const char *arrow = strstr(name, " -> ");
    if (arrow) {
      int n = (int)(arrow - name);
      if (n >= 256) n = 255;
      memcpy(b->name, name, (size_t)n);
      b->name[n] = '\0';
    } else {
      strncpy(b->name, name, sizeof(b->name) - 1);
      b->name[sizeof(b->name) - 1] = '\0';
    }
    // Trim trailing spaces/newline residue
    char *end = b->name + strlen(b->name) - 1;
    while (end >= b->name && (*end == ' ' || *end == '\r')) *end-- = '\0';

    if (b->name[0]) count++;
    line = nl + 1;
  }
  GC_LOG("git_get_branches: %d branches", count);
  return count;
}

// ============================================================
// Public: current branch
// ============================================================

bool git_current_branch(git_repo_t *repo, char *buf, int buf_sz) {
  if (!repo || !buf) return false;
  const char *args[] = { "git", "rev-parse", "--abbrev-ref", "HEAD", NULL };
  bool ok = git_run_sync(repo, args, buf, buf_sz);
  // Strip trailing newline
  char *nl = strchr(buf, '\n');
  if (nl) *nl = '\0';
  return ok;
}

// ============================================================
// Public: remotes
// ============================================================

int git_get_remotes(git_repo_t *repo, char (*out)[256], int max) {
  if (!repo || !out || max <= 0) return 0;

  char buf[4096];
  const char *args[] = { "git", "remote", NULL };
  git_run_sync(repo, args, buf, sizeof(buf));

  int count = 0;
  char *line = buf;
  while (*line && count < max) {
    char *nl = strchr(line, '\n');
    if (!nl) break;
    *nl = '\0';
    if (line[0]) {
      strncpy(out[count], line, 255);
      out[count][255] = '\0';
      count++;
    }
    line = nl + 1;
  }
  return count;
}

// ============================================================
// Async thread
// TODO(platform-C): git_run_async() should be replaced by
// axRunCommandAsync(cmd, op, notify_win, post_msg_id) once the platform
// layer provides a portable async-subprocess API (see file-level TODO
// above).  The git_async_args_t struct and git_async_worker thread function
// below would then be removed entirely.
// ============================================================

typedef struct {
  char            cmd[2048];
  git_op_t        op;
  window_t       *notify_win;
} git_async_args_t;

static GIT_THREAD_RET git_async_worker(void *arg) {
  git_async_args_t *a = (git_async_args_t *)arg;

  git_async_result_t *result =
      (git_async_result_t *)calloc(1, sizeof(git_async_result_t));
  if (!result) { free(a); return (GIT_THREAD_RET)(intptr_t)0; }

  result->op = a->op;

  int rc = gc_popen_read(a->cmd, result->output, sizeof(result->output));
  result->success = (rc == 0);

  GC_LOG("git_async_worker: op=%d rc=%d", (int)a->op, rc);

  post_message(a->notify_win, evGitOpDone, (uint32_t)a->op, result);

  free(a);
  return (GIT_THREAD_RET)(intptr_t)0;
}

bool git_run_async(git_repo_t *repo, git_op_t op,
                   const char *args[],
                   window_t *notify_win) {
  if (!repo || !args || !notify_win) return false;

  git_async_args_t *a =
      (git_async_args_t *)calloc(1, sizeof(git_async_args_t));
  if (!a) return false;

  a->op = op;
  a->notify_win = notify_win;
  gc_build_cmd(repo->path, args, a->cmd, sizeof(a->cmd));

  GC_LOG("git_run_async: %s", a->cmd);

  git_thread_t t;
  if (!git_thread_create(&t, git_async_worker, a)) {
    free(a);
    return false;
  }
  git_thread_detach(t);
  return true;
}

// ============================================================
// Public: free async result
// ============================================================

void git_async_result_free(git_async_result_t *r) {
  free(r);
}
