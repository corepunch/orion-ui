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

#define GC_CMD_BUF_SIZE 2048
#define GC_LOG_PRETTY_FMT "%H\x1f%an\x1f%ad\x1f%s\x1e"
#define GC_LOG_PRETTY_FMT_ESCAPED "%%H\x1f%%an\x1f%%ad\x1f%%s\x1e"

#ifdef _WIN32
#  include <windows.h>
#  include <io.h>    // _access()
#else
#  include <pthread.h>
#  include <sys/stat.h>
#  include <sys/wait.h>
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
//
// Windows note: when the gitclient binary is compiled with MinGW and launched
// from an MSYS2 shell, the inherited PATH is in Unix format which cmd.exe
// cannot parse, so git.exe is not found.  We prepend the two standard Git for
// Windows directories to PATH inside every cmd.exe invocation so that git is
// always reachable regardless of the launch environment.
static void gc_build_cmd(const char *path, const char *args[],
                         char *buf, int buf_sz) {
#ifdef _WIN32
  const int pct_escape_reserve = 5; // extra '%' + original '%' + closing quote + '\0'
  int n = snprintf(buf, (size_t)buf_sz,
      "set \"PATH=C:\\Program Files\\Git\\cmd;"
            "C:\\Program Files\\Git\\bin;"
            "%%PATH%%\" "
      "&& cd \"%s\" && git",
      path);
#else
  int n = snprintf(buf, (size_t)buf_sz, "cd \"%s\" && git", path);
#endif
  for (int i = 1; args[i] && n < buf_sz - 2; i++) {
    n += snprintf(buf + n, (size_t)(buf_sz - n), " \"");
    for (const char *p = args[i]; *p && n < buf_sz - 3; p++) {
#ifdef _WIN32
      if (*p == '%' && n < buf_sz - pct_escape_reserve) buf[n++] = '%'; // cmd.exe requires %% to pass a literal %
#endif
      if (*p == '\"') buf[n++] = '\\';
      buf[n++] = *p;
    }
    n += snprintf(buf + n, (size_t)(buf_sz - n), "\"");
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

static int gc_popen_read_bytes(const char *cmd, char *buf, int buf_sz, int *size_out) {
  if (!cmd || !buf || buf_sz <= 0) return -1;
  FILE *fp = popen(cmd, "r");
  if (!fp) { buf[0] = 0; return -1; }
  int total = 0;
  while (total < buf_sz - 1) {
    size_t n = fread(buf + total, 1, (size_t)(buf_sz - 1 - total), fp);
    total += (int)n;
    if (n == 0) break;
  }
  buf[total] = 0;
  int rc = pclose(fp);
  if (size_out) *size_out = total;
#ifdef _WIN32
  return rc;
#else
  return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#endif
}

#ifdef _WIN32
// Some MinGW/MSYS environments execute popen() through sh instead of cmd.exe.
// If a first run returns literal pretty-format placeholders, collapse %% -> %
// and retry so git receives %H/%an/%ad/%s placeholders.
static void gc_collapse_double_percent(char *s) {
  if (!s) return;
  char *read_pos = s, *write_pos = s;
  while (*read_pos) {
    if (read_pos[0] == '%' && read_pos[1] == '%') {
      *write_pos++ = '%';
      read_pos += 2;
      continue;
    }
    *write_pos++ = *read_pos++;
  }
  *write_pos = '\0';
}
#endif

// ============================================================
// Public: open / close repository
// ============================================================

git_repo_t *git_repo_open(const char *path) {
  if (!path || !path[0]) return NULL;

  // Quick sanity: check .git directory or file exists.
  char check[640];
  snprintf(check, sizeof(check), "%s/.git", path);

  // Use access() on POSIX; _access() on Win32 (both check path existence).
#ifdef _WIN32
  {
    if (_access(check, 0) != 0) {
      GC_LOG("git_repo_open: no .git at %s", path);
      return NULL;
    }
  }
#else
  {
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

bool git_repo_init_path(const char *path) {
  if (!path || !path[0] || !axMkDir(path)) return false;
  git_repo_t repo = {0}; strncpy(repo.path, path, sizeof(repo.path) - 1);
  char buf[2048] = {0}; const char *args[] = { "git", "init", "-b", "main", NULL };
  if (git_run_sync(&repo, args, buf, sizeof(buf))) return true;
  const char *fallback[] = { "git", "init", NULL };
  return git_run_sync(&repo, fallback, buf, sizeof(buf));
}

// ============================================================
// Public: run a git command synchronously
// ============================================================

bool git_run_sync(git_repo_t *repo, const char *args[],
                  char *buf, int buf_sz) {
  if (!repo || !args) return false;
  char cmd[GC_CMD_BUF_SIZE];
  gc_build_cmd(repo->path, args, cmd, sizeof(cmd));
  GC_LOG("git_run_sync: %s", cmd);
  int rc = gc_popen_read(cmd, buf, buf_sz);
  return rc == 0;
}

// ============================================================
// Public: git log
// ============================================================

int git_get_log_ref(git_repo_t *repo, const char *ref,
                    git_commit_t *out, int max) {
  if (!repo || !out || max <= 0) return 0;

  // Format: hash<US>author<US>date<US>subject<RS>
  // Route through gc_build_cmd so that stderr is redirected portably (2>&1).
  char fmt_arg[256];
  snprintf(fmt_arg, sizeof(fmt_arg), "--format=%s", GC_LOG_PRETTY_FMT);
  char count_arg[32];
  snprintf(count_arg, sizeof(count_arg), "--max-count=%d", max);

  const char *args[] = {
    "git", "log", count_arg,
    fmt_arg,
    // Use --date=short (YYYY-MM-DD) to avoid cmd.exe percent expansion issues.
    "--date=short",
    ref && ref[0] ? ref : NULL,
    NULL
  };
  char buf[64 * 1024];
  if (!git_run_sync(repo, args, buf, sizeof(buf))) return 0;
#ifdef _WIN32
  if (strstr(buf, "%H") != NULL && strstr(buf, "%an") != NULL &&
      strstr(buf, "%ad") != NULL && strstr(buf, "%s") != NULL) {
    char cmd[GC_CMD_BUF_SIZE];
    gc_build_cmd(repo->path, args, cmd, sizeof(cmd));
    gc_collapse_double_percent(cmd);
    gc_popen_read(cmd, buf, sizeof(buf));
  }
#endif

  int count = 0;
  char *p = buf;
  while (*p && count < max) {
    git_commit_t *c = &out[count];
    memset(c, 0, sizeof(*c));
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
    if (n >= (int)sizeof(c->author)) n = (int)sizeof(c->author) - 1;
    memcpy(c->author, p, (size_t)n);
    c->author[n] = '\0';
    p = end + 1;

    // date
    end = strchr(p, '\x1f');
    if (!end) break;
    n = (int)(end - p);
    if (n >= (int)sizeof(c->date)) n = (int)sizeof(c->date) - 1;
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
    while (n > 0 && (p[n - 1] == '\r' || p[n - 1] == '\n')) n--;
    if (n >= (int)sizeof(c->subject)) n = (int)sizeof(c->subject) - 1;
    memcpy(c->subject, p, (size_t)n);
    c->subject[n] = '\0';
    p = end + 1;
    while (*p == '\r' || *p == '\n') p++;

    count++;
  }
  GC_LOG("git_get_log: %d commits", count);
  return count;
}

int git_get_log(git_repo_t *repo, git_commit_t *out, int max) {
  return git_get_log_ref(repo, NULL, out, max);
}

// ============================================================
// Public: git status
// ============================================================

int git_get_status(git_repo_t *repo, git_file_status_t *out, int max) {
  if (!repo || !out || max <= 0) return 0;

  char buf[128 * 1024], cmd[GC_CMD_BUF_SIZE]; int size = 0;
  const char *args[] = { "git", "status", "--porcelain=v1", "-z", "-uall", NULL };
  gc_build_cmd(repo->path, args, cmd, sizeof(cmd));
  if (gc_popen_read_bytes(cmd, buf, sizeof(buf), &size) != 0) return 0;

  int count = 0, pos = 0;
  while (pos + 3 < size && count < max) {
    char x = buf[pos], y = buf[pos + 1];
    char *path = buf + pos + 3;
    size_t remain = (size_t)(size - pos - 3);
    size_t path_n = strnlen(path, remain);
    if (path_n >= remain) break;
    pos += 3 + (int)path_n + 1;
    git_file_status_t *f = &out[count++]; memset(f, 0, sizeof(*f));
    f->index_status = x; f->worktree_status = y;
    f->untracked = x == '?' && y == '?';
    f->conflicted = x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D');
    f->staged = !f->untracked && !f->conflicted && x != ' ' && x != '!';
    f->status = f->conflicted ? 'U' : f->untracked ? '?' : f->staged ? x : y;
    strncpy(f->path, path, sizeof(f->path) - 1);
    if ((x == 'R' || x == 'C') && pos < size) {
      char *orig = buf + pos; size_t orig_n = strnlen(orig, (size_t)(size - pos));
      if (orig_n >= (size_t)(size - pos)) break;
      strncpy(f->orig_path, orig, sizeof(f->orig_path) - 1);
      pos += (int)orig_n + 1;
    }
  }
  GC_LOG("git_get_status: %d files", count);
  return count;
}

static void gc_trim_line(char *s) {
  if (!s) return;
  char *e = s + strlen(s); while (e > s && (e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
}

bool git_get_sync_status(git_repo_t *repo, git_sync_status_t *out) {
  if (!repo || !out) return false;
  memset(out, 0, sizeof(*out)); char buf[1024] = {0};
  const char *head[] = { "git", "symbolic-ref", "--short", "-q", "HEAD", NULL };
  if (git_run_sync(repo, head, buf, sizeof(buf))) { gc_trim_line(buf); strncpy(out->head, buf, sizeof(out->head) - 1); }
  else { out->detached = true; strncpy(out->head, "HEAD", sizeof(out->head) - 1); }
  const char *verify[] = { "git", "rev-parse", "--verify", "HEAD", NULL };
  out->initial = !git_run_sync(repo, verify, buf, sizeof(buf));
  const char *dirty[] = { "git", "status", "--porcelain", NULL };
  out->dirty = git_run_sync(repo, dirty, buf, sizeof(buf)) && buf[0];
  if (out->detached || out->initial) return true;

  const char *up[] = { "git", "rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}", NULL };
  if (git_run_sync(repo, up, buf, sizeof(buf))) {
    gc_trim_line(buf); strncpy(out->upstream, buf, sizeof(out->upstream) - 1);
    char *slash = strchr(out->upstream, '/');
    if (slash) snprintf(out->remote, sizeof(out->remote), "%.*s", (int)(slash - out->upstream), out->upstream);
  } else {
    char key[320], merge[512] = {0};
    snprintf(key, sizeof(key), "branch.%s.remote", out->head);
    const char *ra[] = { "git", "config", "--get", key, NULL };
    if (git_run_sync(repo, ra, buf, sizeof(buf))) { gc_trim_line(buf); strncpy(out->remote, buf, sizeof(out->remote) - 1); }
    snprintf(key, sizeof(key), "branch.%s.merge", out->head);
    const char *ma[] = { "git", "config", "--get", key, NULL };
    if (out->remote[0] && git_run_sync(repo, ma, merge, sizeof(merge))) {
      gc_trim_line(merge); const char *name = strrchr(merge, '/'); name = name ? name + 1 : merge;
      snprintf(out->upstream, sizeof(out->upstream), "%s/%s", out->remote, name); out->gone = true;
    }
  }
  if (!out->upstream[0]) return true;
  char range[600];
  if (out->gone) {
    snprintf(range, sizeof(range), "--remotes=%s", out->remote);
    const char *aa[] = { "git", "rev-list", "--count", "HEAD", "--not", range, NULL };
    if (git_run_sync(repo, aa, buf, sizeof(buf))) out->ahead = atoi(buf);
    return true;
  }
  snprintf(range, sizeof(range), "%s..HEAD", out->upstream);
  const char *aa[] = { "git", "rev-list", "--count", range, NULL };
  if (git_run_sync(repo, aa, buf, sizeof(buf))) out->ahead = atoi(buf);
  snprintf(range, sizeof(range), "HEAD..%s", out->upstream);
  const char *bb[] = { "git", "rev-list", "--count", range, NULL };
  if (git_run_sync(repo, bb, buf, sizeof(buf))) out->behind = atoi(buf);
  return true;
}

bool git_get_identity(git_repo_t *repo, char *name, int name_sz,
                      char *email, int email_sz) {
  if (!repo || !name || name_sz <= 0 || !email || email_sz <= 0) return false;
  name[0] = email[0] = 0; char buf[512] = {0};
  const char *na[] = { "git", "config", "--get", "user.name", NULL };
  if (git_run_sync(repo, na, buf, sizeof(buf))) { gc_trim_line(buf); strncpy(name, buf, (size_t)name_sz - 1); }
  const char *ea[] = { "git", "config", "--get", "user.email", NULL };
  if (git_run_sync(repo, ea, buf, sizeof(buf))) { gc_trim_line(buf); strncpy(email, buf, (size_t)email_sz - 1); }
  return name[0] && email[0];
}

bool git_set_identity(git_repo_t *repo, const char *name, const char *email,
                      bool global) {
  if (!repo || !name || !name[0] || !email || !email[0]) return false;
  char buf[1024] = {0};
  const char *na[] = { "git", "config", global ? "--global" : "--local", "user.name", name, NULL };
  if (!git_run_sync(repo, na, buf, sizeof(buf))) return false;
  const char *ea[] = { "git", "config", global ? "--global" : "--local", "user.email", email, NULL };
  return git_run_sync(repo, ea, buf, sizeof(buf));
}

// ============================================================
// Public: git diff
// ============================================================

bool git_get_diff(git_repo_t *repo, const char *path,
                  bool staged, char *buf, int buf_sz) {
  if (!repo || !buf || buf_sz <= 0) return false;

  if (staged) {
    const char *args[] = { "git", "diff", "--color=always", "--cached", "--",
                           path ? path : "", NULL };
    git_run_sync(repo, args, buf, buf_sz);
  } else if (path && path[0]) {
    const char *args[] = { "git", "diff", "--color=always", "--", path, NULL };
    git_run_sync(repo, args, buf, buf_sz);
  } else {
    const char *args[] = { "git", "diff", "--color=always", NULL };
    git_run_sync(repo, args, buf, buf_sz);
  }
  return true;
}

// ============================================================
// Public: branches
// ============================================================

int git_get_branches(git_repo_t *repo, git_branch_t *out, int max) {
  if (!repo || !out || max <= 0) return 0;

  char buf[16 * 1024] = {0};
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
// Public: remote URL
// ============================================================

bool git_get_remote_url(git_repo_t *repo, const char *name, char *buf, int buf_sz) {
  if (!repo || !name || !name[0] || !buf || buf_sz <= 0) return false;
  const char *args[] = { "git", "remote", "get-url", name, NULL };
  if (!git_run_sync(repo, args, buf, buf_sz)) return false;
  char *nl = strchr(buf, '\n');
  if (nl) *nl = '\0';
  return true;
}
// Public: git tags
// ============================================================

int git_get_tags(git_repo_t *repo, git_tag_t *out, int max) {
  if (!repo || !out || max <= 0) return 0;

  // Keep the enumeration command free of pretty-format '%' placeholders:
  // cmd.exe expands them before Git sees them.  Resolve each tag through the
  // already-portable log path to collect its commit hash and date.
  char buf[16 * 1024] = {0};
  const char *args[] = { "git", "tag", "--sort=-creatordate", NULL };
  if (!git_run_sync(repo, args, buf, sizeof(buf))) return 0;

  int count = 0;
  char *p = buf;
  while (*p && count < max) {
    char *nl = strchr(p, '\n');
    if (nl) *nl = '\0';
    char *cr = strchr(p, '\r');
    if (cr) *cr = '\0';

    git_commit_t commit = {0};
    if (p[0] && git_get_log_ref(repo, p, &commit, 1) == 1) {
      git_tag_t *t = &out[count++];
      strncpy(t->name, p, sizeof(t->name) - 1);       t->name[sizeof(t->name) - 1] = '\0';
      strncpy(t->hash, commit.hash, sizeof(t->hash) - 1); t->hash[sizeof(t->hash) - 1] = '\0';
      strncpy(t->date, commit.date, sizeof(t->date) - 1); t->date[sizeof(t->date) - 1] = '\0';
    }
    if (!nl) break;
    p = nl + 1;
  }
  GC_LOG("git_get_tags: %d tags", count);
  return count;
}

// ============================================================
// Public: git stash list
// ============================================================

int git_get_stash(git_repo_t *repo, git_stash_t *out, int max) {
  if (!repo || !out || max <= 0) return 0;

  char buf[16 * 1024] = {0};
  const char *args[] = { "git", "stash", "list", NULL };
  if (!git_run_sync(repo, args, buf, sizeof(buf))) return 0;

  int count = 0;
  char *p = buf;
  while (*p && count < max) {
    char *nl = strchr(p, '\n');
    if (nl) *nl = '\0';
    char *cr = strchr(p, '\r');
    if (cr) *cr = '\0';

    char *ref = p;
    char *msg_sep = strstr(p, ": ");
    if (!msg_sep) { if (!nl) break; p = nl + 1; continue; }
    *msg_sep = '\0';
    char *msg = msg_sep + 2, *branch = msg;
    if (!strncmp(branch, "WIP on ", 7)) branch += 7;
    else if (!strncmp(branch, "On ", 3)) branch += 3;
    char *colon = strchr(branch, ':');
    char branch_buf[256] = "(unnamed)";
    if (colon && colon > branch) {
      size_t len = (size_t)(colon - branch);
      if (len >= sizeof(branch_buf)) len = sizeof(branch_buf) - 1;
      memcpy(branch_buf, branch, len); branch_buf[len] = '\0';
    }

    git_stash_t *s = &out[count];
    strncpy(s->ref, ref, sizeof(s->ref) - 1);                s->ref[sizeof(s->ref) - 1] = '\0';
    strncpy(s->message, msg, sizeof(s->message) - 1);        s->message[sizeof(s->message) - 1] = '\0';
    strncpy(s->branch, branch_buf, sizeof(s->branch) - 1);   s->branch[sizeof(s->branch) - 1] = '\0';
    count++;

    if (!nl) break;
    p = nl + 1;
  }
  GC_LOG("git_get_stash: %d entries", count);
  return count;
}

// ============================================================
// Workspace overview: per-worktree summaries
// ============================================================

static char *gc_next_line(char **p) {
  if (!*p || !**p) return NULL;
  char *s = *p, *nl = strchr(s, '\n'); if (nl) { *nl = 0; *p = nl + 1; } else *p = s + strlen(s);
  return s;
}

// "5 hours ago" -> "5h"; keeps tiles narrow.
static void gc_short_age(const char *rel, char *out, size_t n) {
  int v = atoi(rel); const char *u = strchr(rel, ' '); u = u ? u + 1 : "";
  const char *suffix = !strncmp(u, "second", 6) ? "s" : !strncmp(u, "minute", 6) ? "m" : !strncmp(u, "hour", 4) ? "h" :
                       !strncmp(u, "day", 3) ? "d" : !strncmp(u, "week", 4) ? "w" : !strncmp(u, "month", 5) ? "mo" : !strncmp(u, "year", 4) ? "y" : NULL;
  if (suffix) snprintf(out, n, "%d%s ago", v, suffix); else snprintf(out, n, "%s", rel);
}

int git_worktree_list(const char *path, git_worktree_t *out, int max) {
  git_repo_t repo = {0}; if (!path || !out || max <= 0) return 0;
  strncpy(repo.path, path, sizeof(repo.path) - 1);
  char buf[16 * 1024] = {0}; const char *args[] = { "git", "worktree", "list", "--porcelain", NULL };
  if (!git_run_sync(&repo, args, buf, sizeof(buf))) return 0;
  int count = 0; char *cur = buf;
  for (char *line = gc_next_line(&cur); line && count <= max; line = gc_next_line(&cur)) {
    if (!strncmp(line, "worktree ", 9)) {
      if (count == max) break;
      memset(&out[count], 0, sizeof(out[count]));
      strncpy(out[count].path, line + 9, sizeof(out[count].path) - 1);
      out[count].linked = count > 0; count++;
    } else if (count > 0 && !strcmp(line, "bare")) out[count - 1].bare = true;
    else if (count > 0 && !strncmp(line, "prunable", 8)) out[count - 1].prunable = true;
  }
  return count;
}

static void gc_path_basename(const char *path, char *out, size_t n) {
  const char *end = path + strlen(path); while (end > path && (end[-1] == '/' || end[-1] == '\\')) end--;
  const char *b = end; while (b > path && b[-1] != '/' && b[-1] != '\\') b--;
  snprintf(out, n, "%.*s", (int)(end - b), b);
}

bool git_get_summary(const char *path, const char *repo_name, bool linked, git_summary_t *out) {
  if (!path || !out) return false;
  memset(out, 0, sizeof(*out)); out->linked = linked;
  strncpy(out->path, path, sizeof(out->path) - 1);
  strncpy(out->repo, repo_name && repo_name[0] ? repo_name : "?", sizeof(out->repo) - 1);
  gc_path_basename(path, out->dir, sizeof(out->dir));
  git_repo_t repo = {0}; strncpy(repo.path, path, sizeof(repo.path) - 1);
  static char buf[256 * 1024];
  const char *st[] = { "git", "status", "--porcelain=v2", "--branch", NULL };
  if (!git_run_sync(&repo, st, buf, sizeof(buf))) { out->missing = true; return false; }
  bool has_upstream = false, has_ab = false; char *cur = buf;
  for (char *l = gc_next_line(&cur); l; l = gc_next_line(&cur)) {
    if (!strncmp(l, "# branch.head ", 14)) {
      if (!strcmp(l + 14, "(detached)")) out->detached = true; else strncpy(out->branch, l + 14, sizeof(out->branch) - 1);
    } else if (!strncmp(l, "# branch.upstream ", 18)) { has_upstream = true; strncpy(out->upstream, l + 18, sizeof(out->upstream) - 1); }
    else if (!strncmp(l, "# branch.ab ", 12)) { has_ab = sscanf(l + 12, "+%d -%d", &out->ahead, &out->behind) == 2; }
    else if (l[0] == '?') out->untracked++;
    else if (l[0] == 'u') out->conflicts++;
    else if ((l[0] == '1' || l[0] == '2') && l[1] == ' ' && l[2] && l[3]) {
      if (l[2] != '.') out->staged++;
      if (l[3] != '.') out->unstaged++;
    }
  }
  out->no_upstream = !has_upstream && !out->detached;
  out->gone = has_upstream && !has_ab;
  if (out->detached) {
    const char *sh[] = { "git", "rev-parse", "--short", "HEAD", NULL };
    if (git_run_sync(&repo, sh, buf, sizeof(buf))) { gc_trim_line(buf); snprintf(out->branch, sizeof(out->branch), "detached @ %.16s", buf); }
    else strncpy(out->branch, "detached", sizeof(out->branch) - 1);
  }
  const char *lg[] = { "git", "log", "-1", "--format=%s%x1f%cr", NULL };
  if (git_run_sync(&repo, lg, buf, sizeof(buf))) {
    gc_trim_line(buf); char *sep = strchr(buf, '\x1f');
    if (sep) { *sep = 0; gc_short_age(sep + 1, out->when, sizeof(out->when)); }
    strncpy(out->subject, buf, sizeof(out->subject) - 1);
  } else out->initial = true;
  const char *sl[] = { "git", "stash", "list", NULL };
  if (git_run_sync(&repo, sl, buf, sizeof(buf))) for (char *p = buf; *p; p++) if (*p == '\n') out->stashes++;
  return true;
}

int git_workspace_scan(char (*roots)[512], int root_count, git_summary_t *out, int max) {
  int n = 0;
  for (int i = 0; i < root_count && n < max; i++) {
    git_worktree_t wt[GC_MAX_WORKTREES]; int wn = git_worktree_list(roots[i], wt, GC_MAX_WORKTREES);
    char name[128] = {0};
    if (wn > 0) gc_path_basename(wt[0].path, name, sizeof(name));
    if (wn == 0) { git_summary_t *s = &out[n++]; memset(s, 0, sizeof(*s)); strncpy(s->path, roots[i], sizeof(s->path) - 1);
                   gc_path_basename(roots[i], s->repo, sizeof(s->repo)); strncpy(s->dir, s->repo, sizeof(s->dir) - 1);
                   strncpy(s->branch, "unavailable", sizeof(s->branch) - 1); s->missing = true; continue; }
    for (int w = 0; w < wn && n < max; w++) {
      bool dup = false; for (int k = 0; k < n; k++) if (!strcmp(out[k].path, wt[w].path)) { dup = true; break; }
      if (dup || wt[w].bare) continue;
      if (!git_get_summary(wt[w].path, name, wt[w].linked, &out[n])) { strncpy(out[n].branch, "unavailable", sizeof(out[n].branch) - 1); }
      out[n].prunable = wt[w].prunable; n++;
    }
  }
  return n;
}

typedef struct { char cmds[GC_MAX_RECENT_REPOS][GC_CMD_BUF_SIZE]; int count; window_t *notify_win; } git_fetch_all_args_t;

static GIT_THREAD_RET git_fetch_all_worker(void *arg) {
  git_fetch_all_args_t *a = arg;
  git_async_result_t *res = calloc(1, sizeof(*res));
  if (res) {
    res->op = GIT_OP_GENERIC; res->success = true; int failed = 0;
    for (int i = 0; i < a->count; i++) { char out[512]; if (gc_popen_read(a->cmds[i], out, sizeof(out)) != 0) failed++; }
    snprintf(res->output, sizeof(res->output), "Fetched %d repositories, %d failed.", a->count - failed, failed);
    fprintf(stderr, "[gc] %s\n", res->output); fflush(stderr);
    post_message(a->notify_win, evGitOpDone, (uint32_t)GIT_OP_GENERIC, res);
  }
  free(a); return 0;
}

bool git_fetch_all_async(char (*roots)[512], int count, window_t *notify_win) {
  if (!notify_win || count <= 0) return false;
  git_fetch_all_args_t *a = calloc(1, sizeof(*a)); if (!a) return false;
  a->notify_win = notify_win;
  for (int i = 0; i < count && a->count < GC_MAX_RECENT_REPOS; i++) {
    const char *args[] = { "git", "fetch", "--all", "--prune", NULL };
    gc_build_cmd(roots[i], args, a->cmds[a->count++], GC_CMD_BUF_SIZE);
  }
  git_thread_t t; if (!git_thread_create(&t, git_fetch_all_worker, a)) { free(a); return false; }
  git_thread_detach(t); return true;
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
  char            cmd[GC_CMD_BUF_SIZE];
  git_op_t        op;
  window_t       *notify_win;
} git_async_args_t;

static GIT_THREAD_RET git_async_worker(void *arg) {
  git_async_args_t *a = (git_async_args_t *)arg;

  git_async_result_t *result =
      (git_async_result_t *)calloc(1, sizeof(git_async_result_t));
  if (!result) {
    free(a);
    return 0;
  }

  result->op = a->op;

  int rc = gc_popen_read(a->cmd, result->output, sizeof(result->output));
  result->success = (rc == 0);

  GC_LOG("git_async_worker: op=%d rc=%d", (int)a->op, rc);

  post_message(a->notify_win, evGitOpDone, (uint32_t)a->op, result);

  free(a);
  return 0;
}

bool git_run_async(git_repo_t *repo, git_op_t op,
                   const char *args[],
                   window_t *notify_win) {
  if (!args || !notify_win) return false;

  git_async_args_t *a =
      (git_async_args_t *)calloc(1, sizeof(git_async_args_t));
  if (!a) return false;

  a->op = op;
  a->notify_win = notify_win;
  gc_build_cmd(repo ? repo->path : ".", args, a->cmd, sizeof(a->cmd));

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
