#ifndef __COMIC_STAGE_H__
#define __COMIC_STAGE_H__

#include <orion/ui.h>

#define COMIC_STAGE_CLASS "ComicStage"

// The stage pages between two spreads like UIScrollView paging: a horizontal pan drags the current
// spread aside with the next one beside it, and the release snaps to whichever is nearer once the
// finger's momentum is projected. The stage owns the motion; its root owns the pages:
//
//   COMIC_TURN_QUERY_NEXT / _BACK  evCommand: a turn starts. The root fills and shows the incoming
//                                  spread (slot 1 - front) and returns true, or false at either end,
//                                  where the pan rubber-bands instead.
//   COMIC_TURN_DONE                evCommand: the incoming spread is now in front.
//   COMIC_TURN_CANCEL              evCommand: the turn snapped back; hide the incoming spread.
enum { COMIC_STAGE_CONFIGURE = evUser, COMIC_STAGE_SLIDE, COMIC_STAGE_TURN, COMIC_STAGE_SETTLE, COMIC_STAGE_GRAB,
       COMIC_STAGE_RECTO };
#define COMIC_PAN_SLOP 10   // points before a press becomes a horizontal pan
enum { COMIC_TURN_QUERY_BACK = 10, COMIC_TURN_QUERY_NEXT, COMIC_TURN_DONE, COMIC_TURN_CANCEL };

typedef struct {
  window_t *spread[2];
  window_t *shelf, *back, *palette, *forward;
} comic_stage_layout_t;

// COMIC_STAGE_SLIDE: places the spreads. direction is 1 towards the next spread, -1 back;
// progress runs from 0 (front spread in place) to 1 (incoming spread in place).
typedef struct {
  int front;
  int direction;
  float progress;
} comic_stage_slide_t;

// COMIC_STAGE_TURN: wparam = 1 for the next spread, -1 for the previous one; animates a whole turn.
// Returns false while another turn is moving or when the root refuses it.
// COMIC_STAGE_SETTLE: finishes any turn at once, where it was heading.
// COMIC_STAGE_RECTO: wparam = slot, lparam = true when that spread is a lone right-hand page (a cover
// in landscape): it takes the right half, where it would lie in an open book.
// COMIC_STAGE_GRAB: a child hands over the press it was tracking; wparam = MAKEDWORD(x, y) where it
// went down, in stage coordinates. The stage captures the pointer and follows it from there.
result_t comic_stage_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
bool comic_stage_turning(window_t *win);

#endif
