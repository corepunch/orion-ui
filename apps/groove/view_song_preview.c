// VIEW: a miniature of a saved arrangement, drawn from the song file itself.
// The file is parsed when the path is set, so no thumbnail is kept beside it.
// Display-only: hit-testing hands the point to the containing Card
// (≈ a static control answering HTTRANSPARENT), so a tap selects the song.

#include "groove.h"

#define PREVIEW_MIN_BARS 16

static void paint_preview(const song_t *song, irect16_t r) {
  for (int t = 0; t < GR_TRACKS; t++)
    fill_rect(get_sys_color(t % 2 ? brColumnViewBg : brWindowDarkBg), R(r.x, r.y + t * r.h / GR_TRACKS, r.w, (t + 1) * r.h / GR_TRACKS - t * r.h / GR_TRACKS));
  if (!song) {
    const char *text = ui_tr("Jamsheet cannot read this song");
    draw_text_ellipsized("Text", text, r.x + MAX(0, (r.w - text_strwidth("Text", text)) / 2),
              r.y + (r.h - text_char_height("Text")) / 2, r.w, get_sys_color(brTextSecondary));
    return;
  }
  song_draw_overview(song, r);
}

result_t win_song_preview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)wparam;
  song_t *song = win->userdata;
  switch (msg) {
    case evCreate:
      win->userdata = NULL;
      win->flags |= WINDOW_NOTABSTOP;
      return true;
    case grPreviewSetSong: {
      const char *path = lparam;
      free(song);
      song = path ? malloc(sizeof(*song)) : NULL;
      if (path && !song) {
        fprintf(stderr, "[gr] preview allocation failed win=%u path=%s\n", (unsigned)win->id, path);
        fflush(stderr);
      }
      if (song && !song_read_preview(path, song)) { free(song); song = NULL; }
      win->userdata = song;
      invalidate_window(win);
      return !path || song;
    }
    case evHitTest:
      if (lparam && win->parent) *(window_t **)lparam = win->parent;
      return true;
    case evPaint:
      paint_preview(song, get_client_rect(win));
      return true;
    case evDestroy:
      free(song);
      win->userdata = NULL;
      return true;
    default: return false;
  }
}
