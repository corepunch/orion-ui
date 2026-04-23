// Branch panel — hierarchical list of local branches, remote branches,
// tags, and stashes.  Uses win_reportview in list mode.

#include "gitclient.h"

// ============================================================
// Helpers
// ============================================================

static int count_tags(git_repo_t *repo, char (*out)[256], int max) {
  if (!repo) return 0;
  char buf[8192];
  const char *args[] = { "git", "tag", "--sort=-version:refname", NULL };
  git_run_sync(repo, args, buf, sizeof(buf));
  int n = 0;
  char *line = buf;
  while (*line && n < max) {
    char *nl = strchr(line, '\n');
    if (!nl) break;
    *nl = '\0';
    if (line[0]) { strncpy(out[n], line, 255); out[n][255]='\0'; n++; }
    line = nl + 1;
  }
  return n;
}

static int count_stashes(git_repo_t *repo, char (*out)[256], int max) {
  if (!repo) return 0;
  char buf[8192];
  const char *args[] = { "git", "stash", "list", NULL };
  git_run_sync(repo, args, buf, sizeof(buf));
  int n = 0;
  char *line = buf;
  while (*line && n < max) {
    char *nl = strchr(line, '\n');
    if (!nl) break;
    *nl = '\0';
    if (line[0]) { strncpy(out[n], line, 255); out[n][255]='\0'; n++; }
    line = nl + 1;
  }
  return n;
}

// ============================================================
// Column setup — single full-width column (no header title)
// ============================================================

static void branches_setup_column(window_t *win) {
  send_message(win, RVM_CLEARCOLUMNS, 0, NULL);
  reportview_column_t col = { "", 0 };  /* width=0 → auto-fill available width */
  send_message(win, RVM_ADDCOLUMN, 0, &col);
}

// ============================================================
// Populate
// ============================================================

void gc_branches_refresh(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->branches_win) return;

  window_t *win = gc->branches_win;

  send_message(win, RVM_SETREDRAW, 0, NULL);
  send_message(win, RVM_SETVIEWMODE, RVM_VIEW_REPORT, NULL);
  send_message(win, RVM_CLEAR, 0, NULL);

  if (!gc->repo) {
    send_message(win, RVM_SETREDRAW, 1, NULL);
    return;
  }

  // Reload branch data.
  gc->branch_count = git_get_branches(gc->repo, gc->branches, GC_MAX_BRANCHES);

  // ── LOCAL BRANCHES ────────────────────────────────────────
  {
    reportview_item_t hdr = {
      .text     = "LOCAL BRANCHES",
      .icon     = sysicon_arrow_branch,
      .color    = get_sys_color(brTextDisabled),
      .userdata = 0xFFFC,  // sentinel: not a branch index
    };
    send_message(win, RVM_ADDITEM, 0, &hdr);
  }
  for (int i = 0; i < gc->branch_count; i++) {
    git_branch_t *b = &gc->branches[i];
    if (b->is_remote) continue;
    char label[270];
    if (b->is_current)
      snprintf(label, sizeof(label), "* %s", b->name);
    else
      snprintf(label, sizeof(label), "  %s", b->name);
    reportview_item_t item = {
      .text     = label,
      .icon     = b->is_current ? sysicon_checkmark : sysicon_arrow_right,
      .color    = b->is_current ? get_sys_color(brTextNormal)
                                : get_sys_color(brTextDisabled),
      .userdata = (uint32_t)i,
    };
    send_message(win, RVM_ADDITEM, 0, &item);
  }

  // ── REMOTE BRANCHES ───────────────────────────────────────
  {
    reportview_item_t hdr = {
      .text     = "REMOTE BRANCHES",
      .icon     = sysicon_world,
      .color    = get_sys_color(brTextDisabled),
      .userdata = 0xFFFF,
    };
    send_message(win, RVM_ADDITEM, 0, &hdr);
  }
  for (int i = 0; i < gc->branch_count; i++) {
    git_branch_t *b = &gc->branches[i];
    if (!b->is_remote) continue;
    char label[270];
    snprintf(label, sizeof(label), "  %s", b->name);
    reportview_item_t item = {
      .text     = label,
      .icon     = sysicon_arrow_right,
      .color    = get_sys_color(brTextDisabled),
      .userdata = (uint32_t)i,
    };
    send_message(win, RVM_ADDITEM, 0, &item);
  }

  // ── TAGS ──────────────────────────────────────────────────
  char tags[GC_MAX_BRANCHES][256];
  int tag_count = count_tags(gc->repo, tags, GC_MAX_BRANCHES);
  {
    reportview_item_t hdr = {
      .text     = "TAGS",
      .icon     = sysicon_tag_blue,
      .color    = get_sys_color(brTextDisabled),
      .userdata = 0xFFFE,
    };
    send_message(win, RVM_ADDITEM, 0, &hdr);
  }
  for (int i = 0; i < tag_count; i++) {
    char label[270];
    snprintf(label, sizeof(label), "  %s", tags[i]);
    reportview_item_t item = {
      .text     = label,
      .icon     = sysicon_tag_id,
      .color    = get_sys_color(brTextDisabled),
      .userdata = 0xFFF0u + (uint32_t)i,
    };
    send_message(win, RVM_ADDITEM, 0, &item);
  }

  // ── STASHES ───────────────────────────────────────────────
  char stashes[32][256];
  int stash_count = count_stashes(gc->repo, stashes, 32);
  {
    reportview_item_t hdr = {
      .text     = "STASHES",
      .icon     = sysicon_package,
      .color    = get_sys_color(brTextDisabled),
      .userdata = 0xFFFD,
    };
    send_message(win, RVM_ADDITEM, 0, &hdr);
  }
  for (int i = 0; i < stash_count; i++) {
    char label[270];
    snprintf(label, sizeof(label), "  %s", stashes[i]);
    reportview_item_t item = {
      .text     = label,
      .icon     = sysicon_disk,
      .color    = get_sys_color(brTextDisabled),
      .userdata = 0xFFE0u + (uint32_t)i,
    };
    send_message(win, RVM_ADDITEM, 0, &item);
  }

  send_message(win, RVM_SETREDRAW, 1, NULL);
}

// ============================================================
// Window procedure
// ============================================================

result_t gc_branches_proc(window_t *win, uint32_t msg,
                           uint32_t wparam, void *lparam) {
  result_t r = win_reportview(win, msg, wparam, lparam);

  if (msg == evCreate) {
    send_message(win, RVM_SETVIEWMODE, RVM_VIEW_REPORT, NULL);
    branches_setup_column(win);
    return r;
  }

  if (msg == evResize) {
    branches_setup_column(win);
    return r;
  }

  // Row activated (double-click) → checkout the selected branch.
  if (msg == evCommand && HIWORD(wparam) == RVN_DBLCLK) {
    gc_state_t *gc = g_gc;
    if (!gc || !gc->repo) return r;

    int sel = (int)send_message(win, RVM_GETSELECTION, 0, NULL);
    if (sel < 0) return r;

    // Use the row's stored userdata to find the branch index.
    // Sentinel values (0xFFFF, 0xFFFE, 0xFFFD, 0xFFF0+, 0xFFE0+) indicate
    // header / tag / stash rows — skip them.
    reportview_item_t item = {0};
    send_message(win, RVM_GETITEMDATA, (uint32_t)sel, &item);
    uint32_t ud = item.userdata;
    if (ud >= 0xFF00u) return r;   // header or non-branch sentinel
    int bi = (int)ud;
    if (bi < 0 || bi >= gc->branch_count) return r;

    git_branch_t *b = &gc->branches[bi];
    if (b->is_remote) return r;  // skip remote-only rows
    if (b->is_current) return r; // already on this branch

    const char *args[] = { "git", "checkout", b->name, NULL };
    char buf[512] = {0};
    bool ok = git_run_sync(gc->repo, args, buf, sizeof(buf));
    if (!ok) {
      message_box(win, buf, "Checkout failed", MB_OK);
    } else {
      gc_refresh_all();
    }
    return true;
  }

  // Row selection changed → refresh the log for this branch if needed.
  if (msg == evCommand && HIWORD(wparam) == RVN_SELCHANGE) {
    gc_log_refresh();
    return r;
  }

  return r;
}
