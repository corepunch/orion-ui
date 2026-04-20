#include <stdlib.h>
#include <string.h>

#include "browser.h"

static char *xstrdup(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s) + 1;
  char *p = (char *)malloc(n);
  if (!p) return NULL;
  memcpy(p, s, n);
  return p;
}

bool browser_history_push(browser_state_t *st, const char *url) {
  if (!st || !url || !url[0]) return false;
  if (st->history_index >= 0 && st->history_index < st->history_count &&
      strcmp(st->history[st->history_index], url) == 0) return true;

  for (int i = st->history_index + 1; i < st->history_count; i++) {
    free(st->history[i]);
    st->history[i] = NULL;
  }
  st->history_count = st->history_index + 1;

  if (st->history_count >= st->history_cap) {
    int new_cap = st->history_cap > 0 ? st->history_cap * 2 : 16;
    char **p = (char **)realloc(st->history, (size_t)new_cap * sizeof(char *));
    if (!p) return false;
    st->history = p;
    st->history_cap = new_cap;
  }

  st->history[st->history_count] = xstrdup(url);
  if (!st->history[st->history_count]) return false;
  st->history_count++;
  st->history_index = st->history_count - 1;
  return true;
}

void browser_history_free(browser_state_t *st) {
  if (!st) return;
  for (int i = 0; i < st->history_count; i++) {
    free(st->history[i]);
  }
  free(st->history);
  st->history = NULL;
  st->history_count = 0;
  st->history_cap = 0;
  st->history_index = -1;
}
