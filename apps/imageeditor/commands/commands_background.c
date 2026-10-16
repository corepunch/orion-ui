// commands/commands_background.c — Pencil Test background image commands

#include "commands.h"

bool cmd_background_set_image(canvas_doc_t *doc, bg_image_t *img) {
  if (!pencil_has_layers(doc)) return false;
  if (!ie_doc_begin_op(doc, img ? "Background Image" : "Remove Background Image")) return false;
  bool ok = doc_set_background_image(doc, img);
  ie_doc_commit_op(doc, ok);
  return ok;
}

bool cmd_background_load(canvas_doc_t *doc, const char *path) {
  if (!pencil_has_layers(doc)) return false;
  bg_image_t *img = bg_image_load(path, doc->canvas_w, doc->canvas_h);
  if (!img) return false;
  bool ok = cmd_background_set_image(doc, img);
  bg_image_release(img);
  if (ok && doc->win) send_message(doc->win, evStatusBar, 0, (void *)path);
  return ok;
}

void cmd_background_remove(canvas_doc_t *doc) {
  if (doc && doc->background.image) cmd_background_set_image(doc, NULL);
}
