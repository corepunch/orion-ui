#include "gitclient.h"

// ── Repositories browser ────────────────────────────────────────────────────

typedef struct { int found, depth; char path[512]; } scan_ctx_t;

static void repo_fill(window_t *win) {
  window_t *list = get_window_item(win, ID_REPOSITORIES_DIALOG_REPO_LIST);
  if (!list || !g_gc) return; send_message(list, cbClear, 0, NULL);
  for (int i = 0; i < g_gc->recent_repo_count; i++) send_message(list, cbAddString, 0, g_gc->recent_repos[i]);
  if (g_gc->recent_repo_count) set_window_item_text(win, ID_REPOSITORIES_DIALOG_REPO_LIST, "%s", g_gc->recent_repos[0]);
}

static void scan_path(const char *path, int depth, int *found);
static bool_t scan_entry(AXdirent const *entry, void *userdata) {
  scan_ctx_t *ctx = userdata;
  if (!entry->is_directory || entry->is_hidden || ctx->found >= GC_MAX_RECENT_REPOS) return TRUE;
  if (!strcmp(entry->name, "node_modules") || !strcmp(entry->name, "build") || !strcmp(entry->name, "vendor")) return TRUE;
  char child[512]; snprintf(child, sizeof(child), "%s/%s", ctx->path, entry->name);
  scan_path(child, ctx->depth + 1, &ctx->found); return TRUE;
}

static void scan_path(const char *path, int depth, int *found) {
  if (!path || depth > 7 || *found >= GC_MAX_RECENT_REPOS) return;
  char marker[560]; snprintf(marker, sizeof(marker), "%s/.git", path);
  if (axPathExists(marker)) { gc_recent_add(path); (*found)++; return; }
  scan_ctx_t ctx = {.found = *found, .depth = depth}; strncpy(ctx.path, path, sizeof(ctx.path) - 1);
  axListDir(path, scan_entry, &ctx); *found = ctx.found;
}

static result_t repos_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)lparam;
  if (msg == evCreate) { repo_fill(win); return true; }
  if (msg != evCommand || HIWORD(wparam) != btnClicked) return false;
  uint16_t id = LOWORD(wparam);
  if (id == ID_REPOSITORIES_DIALOG_CLOSE) { end_dialog(win, 0); return true; }
  if (id == ID_REPOSITORIES_DIALOG_ADD) {
    char path[512] = {0}; openfilename_t ofn = {.lStructSize=sizeof(ofn), .lpstrFile=path, .nMaxFile=sizeof(path), .Flags=OFN_PICKFOLDER};
    if (get_folder_name(&ofn)) { git_repo_t *r = git_repo_open(path); if (r) { gc_recent_add(git_repo_path(r)); git_repo_close(r); repo_fill(win); } }
    return true;
  }
  if (id == ID_REPOSITORIES_DIALOG_SCAN) {
    char path[512] = {0}; openfilename_t ofn = {.lStructSize=sizeof(ofn), .lpstrFile=path, .nMaxFile=sizeof(path), .Flags=OFN_PICKFOLDER};
    if (get_folder_name(&ofn)) { int found = 0; scan_path(path, 0, &found); repo_fill(win); }
    return true;
  }
  window_t *list = get_window_item(win, ID_REPOSITORIES_DIALOG_REPO_LIST); char path[512] = {0};
  if (list) strncpy(path, list->title, sizeof(path) - 1);
  if (id == ID_REPOSITORIES_DIALOG_OPEN && path[0]) { end_dialog(win, 1); gc_add_repo(path); return true; }
  if (id == ID_REPOSITORIES_DIALOG_REMOVE && path[0] && g_gc) {
    for (int i = 0; i < g_gc->recent_repo_count; i++) if (!strcmp(g_gc->recent_repos[i], path)) {
      memmove(g_gc->recent_repos[i], g_gc->recent_repos[i + 1], (size_t)(g_gc->recent_repo_count - i - 1) * 512);
      g_gc->recent_repo_count--; gc_recent_save(); repo_fill(win); break;
    }
    return true;
  }
  return false;
}

void gc_show_repositories_dialog(window_t *parent) {
  show_dialog_from_form(&gitclient_repositories_dialog_form, "Repositories", parent, repos_proc, NULL);
}

// ── Create repository ────────────────────────────────────────────────────────

typedef struct { char path[512]; } create_repo_state_t;

static const ctrl_binding_t create_repo_bindings[] = {
  DDX_TEXT(ID_CREATE_REPO_DIALOG_PATH, create_repo_state_t, path),
};

static result_t create_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  create_repo_state_t *st = (create_repo_state_t *)win->userdata;
  if (msg == evCreate) {
    win->userdata = lparam; st = (create_repo_state_t *)lparam;
    dialog_push(win, st, create_repo_bindings, ARRAY_LEN(create_repo_bindings));
    return true;
  }
  if (msg != evCommand || HIWORD(wparam) != btnClicked) return false;
  uint16_t id = LOWORD(wparam);
  if (id == ID_CREATE_REPO_DIALOG_CANCEL) { end_dialog(win, 0); return true; }
  if (id == ID_CREATE_REPO_DIALOG_BROWSE) {
    char path[512] = {0}; openfilename_t ofn = {.lStructSize=sizeof(ofn), .lpstrFile=path, .nMaxFile=sizeof(path), .Flags=OFN_PICKFOLDER};
    if (get_folder_name(&ofn)) { strncpy(st->path, path, sizeof(st->path) - 1); dialog_push(win, st, create_repo_bindings, ARRAY_LEN(create_repo_bindings)); }
    return true;
  }
  if (id == ID_CREATE_REPO_DIALOG_OK) {
    dialog_pull(win, st, create_repo_bindings, ARRAY_LEN(create_repo_bindings));
    if (st->path[0] && gc_init_repo(st->path)) { end_dialog(win, 1); gc_add_repo(st->path); }
    else set_window_item_text(win, ID_CREATE_REPO_DIALOG_STATUS, "Could not create the repository.");
    return true;
  }
  return false;
}

void gc_show_create_repo_dialog(window_t *parent) {
  create_repo_state_t st = {0};
  show_dialog_from_form(&gitclient_create_repo_dialog_form, "Create Repository", parent, create_proc, &st);
}
