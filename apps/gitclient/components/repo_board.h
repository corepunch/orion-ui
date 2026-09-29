#ifndef __GC_REPO_BOARD_H__
#define __GC_REPO_BOARD_H__

#include <stdbool.h>
#include <orion/ui.h>

#define GC_REPO_BOARD_CLASS_NAME "RepoBoard"

// Notifications sent to the root window as evCommand: LOWORD = tile index, HIWORD = code.
#define GC_BOARD_SELECT 1201
#define GC_BOARD_OPEN   1202

enum { rbSetTiles = evUser + 600, rbGetSelection, rbSetSelection, rbSetFilter };

// One overview tile: the at-a-glance state of a single working tree.
typedef struct {
  char path[512], repo[96], dir[96], branch[96], upstream[96], subject[160], when[32];
  int  staged, unstaged, untracked, conflicts, ahead, behind, stashes;
  bool linked, detached, no_upstream, gone, missing, initial, prunable;
} gc_tile_t;

static inline bool gc_tile_needs_attention(const gc_tile_t *t) {
  return t->missing || t->conflicts || t->staged || t->unstaged || t->untracked || t->ahead || t->behind || t->no_upstream || t->gone;
}

result_t gc_repo_board_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

#endif
