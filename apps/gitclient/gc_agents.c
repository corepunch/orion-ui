#include "gc_agents.h"
#include <ctype.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#endif

typedef enum { AGENT_CREATE, AGENT_SNAPSHOT, AGENT_PREVIEW } gc_agent_operation_t;

typedef struct gc_agent_work_s {
  gc_agent_task_t *task;
  axmutex_t mutex;
  gc_agent_operation_t op;
  bool done;
  const char *error;
  char detail[1024], revision[65], target[64], command[3072];
} gc_agent_work_t;

typedef struct gc_agents_s {
  gc_agent_task_t *first, *last;
  uint32_t timer;
  int next_id;
} gc_agents_t;

bool gc_agents_supported(void) {
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
  return false;
#else
  return true;
#endif
}

const char *gc_agent_provider_name(gc_agent_provider_t provider) {
  return provider == GC_AGENT_CLAUDE ? "Claude Code" : "Codex";
}

const gc_agent_task_t *gc_agents_tasks(void) {
  return g_gc && g_gc->agents ? g_gc->agents->first : NULL;
}

static gc_agent_task_t *agent_find(int id) {
  if (!g_gc || !g_gc->agents) return NULL;
  for (gc_agent_task_t *t = g_gc->agents->first; t; t = t->next)
    if (t->id == id) return t;
  return NULL;
}

static bool agent_oid(git_repo_t *repo, char out[65], char *detail, size_t cap) {
  const char *args[] = {"git", "rev-parse", "--verify", "HEAD^{commit}", NULL};
  if (!git_run_sync(repo, args, detail, (int)cap)) return false;
  size_t n = strcspn(detail, "\r\n");
  if (n != 40 && n != 64) return false;
  for (size_t i = 0; i < n; i++) if (!isxdigit((unsigned char)detail[i])) return false;
  memcpy(out, detail, n); out[n] = 0;
  detail[0] = 0;
  return true;
}

static const char *agent_create_worktree(gc_agent_work_t *w) {
  gc_agent_task_t *t = w->task;
  git_repo_t *repo = git_repo_open(t->source);
  const char *args[] = {"git", "worktree", "add", "-b", t->branch, "--", t->checkout, t->base, NULL};
  bool ok = git_run_sync(repo, args, w->detail, sizeof(w->detail));
  git_repo_close(repo);
  return ok ? NULL : "Could not create the agent worktree.";
}

static const char *agent_capture_snapshot(gc_agent_work_t *w) {
  gc_agent_task_t *t = w->task;
  git_repo_t *repo = git_repo_open(t->checkout);
  const char *branch[] = {"git", "symbolic-ref", "--short", "HEAD", NULL};
  const char *ancestor[] = {"git", "merge-base", "--is-ancestor", t->base, "HEAD", NULL};
  const char *status[] = {"git", "status", "--porcelain=v1", "--untracked-files=normal", NULL};
  const char *add[] = {"git", "add", "-A", "--", ".", NULL};
  const char *commit[] = {"git", "-c", "user.name=Git Client", "-c", "user.email=gitclient@localhost",
                          "commit", "--no-gpg-sign", "-m", "Agent task snapshot", NULL};
  const char *error = "The agent left its task branch or changed its base.";
  if (!git_run_sync(repo, branch, w->detail, sizeof(w->detail))) goto done;
  w->detail[strcspn(w->detail, "\r\n")] = 0;
  if (strcmp(w->detail, t->branch) || !git_run_sync(repo, ancestor, w->detail, sizeof(w->detail))) goto done;

  error = "Could not save the agent revision.";
  if (!git_run_sync(repo, status, w->detail, sizeof(w->detail))) goto done;
  if (w->detail[0] && (!git_run_sync(repo, add, w->detail, sizeof(w->detail)) ||
                      !git_run_sync(repo, commit, w->detail, sizeof(w->detail)))) goto done;
  if (!agent_oid(repo, w->revision, w->detail, sizeof(w->detail))) goto done;
  const char *diff[] = {"git", "diff", "--no-ext-diff", "--no-textconv", "--name-only", t->base, w->revision, "--", NULL};
  if (!git_run_sync(repo, diff, w->detail, sizeof(w->detail))) goto done;
  if (!w->detail[0]) w->revision[0] = 0;
  if (w->revision[0]) {
    // Keep the reviewed commit reachable even when the agent branch later moves.
    char ref[144];
    snprintf(ref, sizeof(ref), "refs/gitclient/%s", t->branch);
    const char *pin[] = {"git", "update-ref", ref, w->revision, NULL};
    if (!git_run_sync(repo, pin, w->detail, sizeof(w->detail))) goto done;
  }
  error = NULL;
done:
  git_repo_close(repo);
  return error;
}

static const char *agent_preview_command(gc_agent_work_t *w) {
  gc_agent_task_t *t = w->task;
  if (!axPathExists(t->preview)) {
    git_repo_t *source = git_repo_open(t->source);
    const char *add[] = {"git", "worktree", "add", "--detach", "--", t->preview, t->revision, NULL};
    bool ok = git_run_sync(source, add, w->detail, sizeof(w->detail));
    git_repo_close(source);
    if (!ok) return "Could not create a checkout for the reviewed revision.";
  }

  git_repo_t *repo = git_repo_open(t->preview);
  char actual[65], path[540];
  const char *error = "The launch checkout is no longer at the reviewed revision.";
  if (!agent_oid(repo, actual, w->detail, sizeof(w->detail)) || strcmp(actual, t->revision)) goto done;
  const char *status[] = {"git", "status", "--porcelain=v1", "--untracked-files=normal", "--ignore-submodules=none", NULL};
  error = "The launch checkout has local changes or could not be read.";
  if (!git_run_sync(repo, status, w->detail, sizeof(w->detail)) || w->detail[0]) goto done;

  error = "The reviewed revision has no usable application target in launch.xml.";
  const char *manifest[] = {"git", "ls-tree", "--full-tree", t->revision, "--", "launch.xml", NULL};
  if (!git_run_sync(repo, manifest, w->detail, sizeof(w->detail)) ||
      (strncmp(w->detail, "100644 blob ", 12) && strncmp(w->detail, "100755 blob ", 12))) goto done;
  snprintf(path, sizeof(path), "%s/launch.xml", t->preview);
#ifndef _WIN32
  struct stat st;
  if (lstat(path, &st) || !S_ISREG(st.st_mode)) goto done;
#endif
  gc_launch_t *launch = calloc(1, sizeof(*launch));
  bool runnable = launch && gc_launch_load(t->preview, launch) > 0 && launch->default_index >= 0;
  if (runnable) {
    const gc_launch_target_t *target = &launch->targets[launch->default_index];
    runnable = gc_launch_command(target, NULL, w->command, sizeof(w->command));
    if (runnable) snprintf(w->target, sizeof(w->target), "%s", target->name);
  }
  free(launch);
  if (!runnable) goto done;

  snprintf(path, sizeof(path), "%s/.gitmodules", t->preview);
  if (axPathExists(path)) {
    const char *modules[] = {"git", "submodule", "update", "--init", "--recursive", NULL};
    error = "Could not check out the submodules at the reviewed revision.";
    if (!git_run_sync(repo, modules, w->detail, sizeof(w->detail))) goto done;
  }
  error = NULL;
done:
  git_repo_close(repo);
  return error;
}

static void agent_work(void *arg) {
  gc_agent_work_t *w = arg;
  switch (w->op) {
    case AGENT_CREATE:   w->error = agent_create_worktree(w); break;
    case AGENT_SNAPSHOT: w->error = agent_capture_snapshot(w); break;
    case AGENT_PREVIEW:  w->error = agent_preview_command(w); break;
  }
  axMutexLock(w->mutex);
  w->done = true;
  axMutexUnlock(w->mutex);
}

static void agent_work_free(gc_agent_task_t *t) {
  if (!t->work) return;
  axThreadJoin(t->worker);
  axMutexDestroy(t->work->mutex);
  free(t->work);
  t->work = NULL;
  t->worker = NULL;
}

static bool agent_timer(void) {
  gc_agents_t *a = g_gc->agents;
  if (!a->timer) a->timer = axSetTimer(g_gc->main_win, 150, NULL, true);
  return a->timer != 0;
}

static bool agent_begin_work(gc_agent_task_t *t, gc_agent_operation_t op) {
  gc_agent_work_t *w = calloc(1, sizeof(*w));
  if (!w) return false;
  w->mutex = axMutexCreate();
  w->op = op;
  w->task = t;
  if (w->mutex && agent_timer()) t->worker = axThreadCreate(agent_work, w);
  if (!t->worker) {
    if (w->mutex) axMutexDestroy(w->mutex);
    free(w);
    return false;
  }
  t->work = w;
  return true;
}

static void agent_failed(gc_agent_task_t *t, const char *error, const char *detail) {
  t->phase = GC_AGENT_FAILED;
  t->error = error;
  fprintf(stderr, "[gitclient] agent task=%d: %s %s\n", t->id, error, detail ? detail : "");
  fflush(stderr);
}

static bool agent_start_process(gc_agent_task_t *t) {
  char cmd[GC_AGENT_PROMPT_SIZE * 4 + 384];
  const char *prefix = t->request.provider == GC_AGENT_CLAUDE
    ? "exec claude --print --permission-mode acceptEdits --"
    : "exec codex -a never exec -s workspace-write --";
  snprintf(cmd, sizeof(cmd), "unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR; %s", prefix);
  char prompt[GC_AGENT_PROMPT_SIZE + 208];
  snprintf(prompt, sizeof(prompt), "%s\n\nWork in this assigned worktree. Leave your changes here for Git Client to snapshot. Do not push or change branches.", t->request.prompt);
  return gc_shell_append_arg(cmd, sizeof(cmd), prompt) &&
         gc_run_start(t->checkout, gc_agent_provider_name(t->request.provider), cmd);
}

#ifndef _WIN32
static bool agent_prompt_nonempty(const char *prompt) {
  while (*prompt && isspace((unsigned char)*prompt)) prompt++;
  return *prompt != 0;
}
#endif

bool gc_agents_start(const char *source, const char *storage, const gc_agent_request_t requests[GC_AGENT_BATCH_SIZE]) {
#ifdef _WIN32
  (void)source; (void)storage; (void)requests;
  return false;
#else
  if (!gc_agents_supported() || !g_gc || !g_gc->main_win || !source || !storage || !requests) return false;
  for (int i = 0; i < GC_AGENT_BATCH_SIZE; i++)
    if (!agent_prompt_nonempty(requests[i].prompt)) return false;
  git_repo_t *repo = git_repo_open(source);
  char base[65], detail[1024] = {0};
  bool have_base = agent_oid(repo, base, detail, sizeof(detail));
  git_repo_close(repo);
  if (!have_base) return false;

  char parent[512], batch[512], root[512];
  if (snprintf(parent, sizeof(parent), "%s/gitclient-worktrees", storage) >= (int)sizeof(parent) ||
      !axMkDir(parent) || !git_path_absolute(parent, root, sizeof(root)) ||
      snprintf(batch, sizeof(batch), "%s/batch-XXXXXXXX", root) >= (int)sizeof(batch) - 24) return false;

  if (!g_gc->agents && !(g_gc->agents = calloc(1, sizeof(*g_gc->agents)))) return false;
  gc_agents_t *a = g_gc->agents;
  gc_agent_task_t *items[GC_AGENT_BATCH_SIZE] = {0};
  for (int i = 0; i < GC_AGENT_BATCH_SIZE; i++) {
    items[i] = calloc(1, sizeof(*items[i]));
    if (!items[i]) goto fail;
  }
  if (!mkdtemp(batch)) goto fail;
  const char *name = strrchr(batch, '/') + 1;
  for (int i = 0; i < GC_AGENT_BATCH_SIZE; i++) {
    gc_agent_task_t *t = items[i];
    t->id = ++a->next_id;
    t->request = requests[i];
    t->phase = GC_AGENT_PREPARING;
    snprintf(t->source, sizeof(t->source), "%s", source);
    snprintf(t->base, sizeof(t->base), "%s", base);
    snprintf(t->branch, sizeof(t->branch), "gitclient/%s-%d", name, i + 1);
    snprintf(t->checkout, sizeof(t->checkout), "%s/agent-%d", batch, i + 1);
    snprintf(t->preview, sizeof(t->preview), "%s/review-%d", batch, i + 1);
    if (a->last) a->last->next = t; else a->first = t;
    a->last = t;
    if (!agent_begin_work(t, AGENT_CREATE)) agent_failed(t, "Could not start worktree preparation.", NULL);
  }
  gc_agents_refresh();
  return true;
fail:
  for (int i = 0; i < GC_AGENT_BATCH_SIZE; i++) free(items[i]);
  return false;
#endif
}

static bool agent_apply_work(gc_agent_task_t *t) {
  gc_agent_work_t *w = t->work;
  if (!w) return false;
  axMutexLock(w->mutex);
  bool done = w->done;
  axMutexUnlock(w->mutex);
  if (!done) return false;
  if (w->op == AGENT_PREVIEW) {
    t->error = w->error;
    if (!t->error && gc_run_start(t->preview, w->target, w->command)) {
      snprintf(t->target, sizeof(t->target), "%s", w->target);
      gc_run_reveal(t->preview, t->target);
    } else {
      if (!t->error) t->error = "Could not start the application target.";
      fprintf(stderr, "[gitclient] launch task=%d: %s %s\n", t->id, t->error, w->detail);
      fflush(stderr);
    }
  } else if (w->error) agent_failed(t, w->error, w->detail);
  else if (w->op == AGENT_CREATE) {
    if (agent_start_process(t)) t->phase = GC_AGENT_RUNNING;
    else agent_failed(t, "Could not start the agent process.", NULL);
  } else {
    snprintf(t->revision, sizeof(t->revision), "%s", w->revision);
    t->phase = t->revision[0] ? GC_AGENT_READY : GC_AGENT_NO_CHANGES;
  }
  agent_work_free(t);
  return true;
}

bool gc_agents_poll(uint32_t timer) {
  gc_agents_t *a = g_gc ? g_gc->agents : NULL;
  if (!a || !a->timer || timer != a->timer) return false;
  bool active = false, changed = false;
  for (gc_agent_task_t *t = a->first; t; t = t->next) {
    changed |= agent_apply_work(t);
    if (t->phase == GC_AGENT_RUNNING || t->phase == GC_AGENT_STOPPING) {
      int exit_code = -1;
      bool found = gc_run_result(t->checkout, gc_agent_provider_name(t->request.provider), &exit_code);
      if (!found || exit_code >= 0) {
        changed = true;
        if (t->phase == GC_AGENT_STOPPING || !found || exit_code == 130 || exit_code == 143) t->phase = GC_AGENT_STOPPED;
        else if (exit_code == 0) {
          t->phase = GC_AGENT_SNAPSHOTTING;
          if (!agent_begin_work(t, AGENT_SNAPSHOT)) agent_failed(t, "Could not start revision capture.", NULL);
        } else {
          char code[32]; snprintf(code, sizeof(code), "%d", exit_code);
          agent_failed(t, "The agent exited with an error. See Agent Output.", code);
        }
      }
    }
    active |= t->work || t->phase == GC_AGENT_RUNNING || t->phase == GC_AGENT_STOPPING;
  }
  if (changed) gc_agents_refresh();
  if (!active) { axCancelTimer(a->timer); a->timer = 0; }
  return true;
}

bool gc_agent_stop(int id) {
  gc_agent_task_t *t = agent_find(id);
  if (!t || (t->phase != GC_AGENT_RUNNING && t->phase != GC_AGENT_STOPPING)) return false;
  const char *name = gc_agent_provider_name(t->request.provider);
  if (!gc_run_is_running(t->checkout, name) || !gc_run_stop(t->checkout, name)) return false;
  t->phase = GC_AGENT_STOPPING;
  gc_agents_refresh();
  return true;
}

bool gc_agent_output(int id) {
  gc_agent_task_t *t = agent_find(id);
  return t && gc_run_reveal(t->checkout, gc_agent_provider_name(t->request.provider));
}

bool gc_agent_has_output(const gc_agent_task_t *t) {
  return t && gc_run_result(t->checkout, gc_agent_provider_name(t->request.provider), NULL);
}

bool gc_agent_diff(int id, char *out, size_t cap) {
  gc_agent_task_t *t = agent_find(id);
  if (!t || t->phase != GC_AGENT_READY || !out || cap == 0) return false;
  git_repo_t *repo = git_repo_open(t->source);
  const char *args[] = {"git", "diff", "--no-ext-diff", "--no-textconv", "--no-color", t->base, t->revision, "--", NULL};
  bool ok = git_run_sync(repo, args, out, (int)cap);
  git_repo_close(repo);
  return ok;
}

bool gc_agent_launch(int id) {
  gc_agent_task_t *t = agent_find(id);
  if (!t || t->phase != GC_AGENT_READY || t->work) return false;
  if (t->target[0] && gc_run_is_running(t->preview, t->target)) return gc_run_reveal(t->preview, t->target);
  bool ok = agent_begin_work(t, AGENT_PREVIEW);
  if (ok) { t->error = NULL; gc_agents_refresh(); }
  return ok;
}

void gc_agents_shutdown(void) {
  gc_agents_t *a = g_gc ? g_gc->agents : NULL;
  if (!a) return;
  if (a->timer) axCancelTimer(a->timer);
  while (a->first) {
    gc_agent_task_t *t = a->first;
    a->first = t->next;
    agent_work_free(t);
    free(t);
  }
  free(a);
  g_gc->agents = NULL;
}
