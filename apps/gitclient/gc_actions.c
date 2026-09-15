// Canonical action dispatch for gitclient.

#include "gc_actions.h"

static const char *gc_action_name(uint16_t id) {
  for (int i = 0; i < gitclient_action_meta_count; i++)
    if (gitclient_action_meta[i].id == id) return gitclient_action_meta[i].name;
  return "(unknown)";
}

static bool gc_action_has_handler(uint16_t id) {
  switch (id) {
    case ID_FILE_OPEN_REPO: case ID_FILE_REPOSITORIES: case ID_FILE_NEW_REPO:
    case ID_FILE_CLONE: case ID_FILE_QUIT:
    case ID_REPO_REFRESH: case ID_REPO_SEARCH: case ID_REPO_IDENTITY:
    case ID_REPO_TERMINAL:
    case ID_VIEW_WINDOW_MODE: case ID_VIEW_CHANGES: case ID_VIEW_HISTORY: case ID_VIEW_GITHUB:
    case ID_BRANCH_NEW: case ID_BRANCH_CHECKOUT: case ID_BRANCH_MERGE:
    case ID_BRANCH_REBASE: case ID_BRANCH_DELETE: case ID_BRANCH_RENAME:
    case ID_COMMIT_COMMIT: case ID_COMMIT_AMEND: case ID_COMMIT_UNDO:
    case ID_COMMIT_STASH: case ID_COMMIT_STASH_POP: case ID_COMMIT_STASH_DROP:
    case ID_COMMIT_DISCARD:
    case ID_FILES_STAGE: case ID_FILES_UNSTAGE: case ID_FILES_STAGE_ALL:
    case ID_FILES_UNSTAGE_ALL: case ID_FILES_REVEAL: case ID_FILES_DISCARD:
    case ID_REMOTE_SYNC: case ID_REMOTE_FETCH: case ID_REMOTE_PULL:
    case ID_REMOTE_PUSH: case ID_REMOTE_MANAGE:
    case ID_TAG_CREATE: case ID_TAG_DELETE: case ID_TAG_PUSH:
    case ID_HELP_ABOUT:
      return true;
    default:
      return false;
  }
}

bool gc_action_handler_for(uint16_t id, const char **name) {
  if (!gc_action_has_handler(id)) return false;
  if (name) *name = gc_action_name(id);
  return true;
}

gc_action_result_t gc_execute_action(uint16_t id) {
  gc_state_t *gc = g_gc;
  const char *name = NULL;
  if (!gc_action_handler_for(id, &name)) {
    GC_TRACE("action rejected id=%d name=%s reason=no-handler",
             (int)id, gc_action_name(id));
    return GC_ACTION_UNAVAILABLE;
  }

  GC_TRACE("action id=%d name=%s repo=%s view=%s commit=%d file=%d",
           (int)id, name,
           gc && gc->repo ? git_repo_path(gc->repo) : "(none)",
           gc && gc->history_mode ? "history" : "changes",
           gc ? gc->selected_commit : -1,
           gc ? gc->selected_file : -1);
  gc_handle_command_impl(id);
  return GC_ACTION_DONE;
}

// Compatibility entry point used by Orion's menu callback API.
void gc_handle_command(uint16_t id) {
  (void)gc_execute_action(id);
}
