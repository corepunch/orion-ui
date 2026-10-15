#ifndef __ANIMATION_DB_H__
#define __ANIMATION_DB_H__
#include "imageeditor.h"

typedef struct {
  int id, cel, delay, uses;
  char name[32], note[128];
} animation_row_t;

typedef struct {
  canvas_doc_t *doc;
  animation_row_t *bank, *sheet;
} animation_db_state_t;

database_t *animation_db_bind(animation_db_state_t *state);
void animation_db_unbind(database_t *db);
#endif
