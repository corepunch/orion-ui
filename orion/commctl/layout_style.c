// A container's spacing, padding and (for a StackView) direction from its style
// sheet: `gap`, `padding` and `flex-direction`, as a div would have them.
// What the form or code declares stands (layout_t.layout_sheet names what it
// leaves to the sheet), the way an inline style beats a rule.
#include <string.h>
#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/ui_style.h>
#include "commctl_internal.h"

static int layout_style_gap(const char *sel, const char *property) {
  int gap = ui_px(sel, property);
  return gap < 0 ? 0 : gap > 255 ? 255 : gap;
}

bool layout_style_resolve(window_t *win, const char *element, layout_gap_axis_t gap_axis) {
  if (!win || !(win->layout.layout_sheet & LAYOUT_SHEET_ALL)) return false;
  const char *sel = ui_selector(win, element);
  layout_t was = win->layout;
  flags_t was_flags = win->flags;
  uint8_t sheet = win->layout.layout_sheet;
  if ((sheet & LAYOUT_SHEET_ORIENT) && gap_axis == LAYOUT_GAP_MAIN) {
    bool row = ui_keyword_is(sel, "flex-direction", "row");
    win->flags = row ? win->flags | WINDOW_STACK_HORIZONTAL : win->flags & ~WINDOW_STACK_HORIZONTAL;
  }
  if (sheet & LAYOUT_SHEET_SPACING) {
    const char *property = gap_axis == LAYOUT_GAP_MAIN ? (layout_is_horizontal(win) ? "column-gap" : "row-gap") : "row-gap";
    win->layout.layout_spacing = (uint8_t)layout_style_gap(sel, property);
  }
  if (sheet & LAYOUT_SHEET_PADDING)
    win->layout.layout_padding = (irect16_t){ ui_px(sel, "padding-left"), ui_px(sel, "padding-top"),
                                              ui_px(sel, "padding-right"), ui_px(sel, "padding-bottom") };
  return was.layout_spacing != win->layout.layout_spacing || was_flags != win->flags ||
         memcmp(&was.layout_padding, &win->layout.layout_padding, sizeof(was.layout_padding)) != 0;
}

// The sheet or the theme changed: take the new values and, if the container
// was already laid out, lay its window out again.
void layout_style_refresh(window_t *win, const char *element, layout_gap_axis_t gap_axis) {
  if (!layout_style_resolve(win, element, gap_axis) || win->frame.w <= 0) return;
  window_t *root = win;
  while (root->parent) root = root->parent;
  post_message(root, evResize, 0, NULL);
}
