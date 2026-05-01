// CONTROLLER: Application state and business logic.

#include "socialfeed.h"

// ============================================================
// Global app-state pointer
// ============================================================

app_state_t *g_app = NULL;

// ============================================================
// app_init
// ============================================================

app_state_t *app_init(void) {
  app_state_t *app = (app_state_t *)calloc(1, sizeof(app_state_t));
  if (!app) return NULL;

  app->post_cap = POSTS_INIT_CAP;
  app->posts    = (post_t **)calloc((size_t)app->post_cap, sizeof(post_t *));
  if (!app->posts) {
    free(app);
    return NULL;
  }

  app->next_id      = 1;
  app->selected_idx = -1;
  return app;
}

// ============================================================
// app_shutdown
// ============================================================

void app_shutdown(app_state_t *app) {
  if (!app) return;
  for (int i = 0; i < app->post_count; i++)
    post_free(app->posts[i]);
  free(app->posts);
  if (app->accel)
    free_accelerators(app->accel);
  free(app);
}

// ============================================================
// app_add_post — append a post, grow the array if needed
// ============================================================

bool app_add_post(post_t *post) {
  if (!g_app || !post) return false;
  if (g_app->post_count >= g_app->post_cap) {
    int new_cap = g_app->post_cap * 2;
    post_t **newbuf = (post_t **)realloc(g_app->posts,
                                         (size_t)new_cap * sizeof(post_t *));
    if (!newbuf) return false;
    g_app->posts    = newbuf;
    g_app->post_cap = new_cap;
  }
  post->id = g_app->next_id++;
  g_app->posts[g_app->post_count++] = post;
  return true;
}

// ============================================================
// app_delete_post — remove post at index and free it
// ============================================================

bool app_delete_post(int index) {
  if (!g_app || index < 0 || index >= g_app->post_count) return false;
  post_free(g_app->posts[index]);
  for (int i = index; i < g_app->post_count - 1; i++)
    g_app->posts[i] = g_app->posts[i + 1];
  g_app->post_count--;
  if (g_app->selected_idx >= g_app->post_count)
    g_app->selected_idx = g_app->post_count - 1;
  return true;
}

// ============================================================
// app_get_post — bounds-checked accessor
// ============================================================

post_t *app_get_post(int index) {
  if (!g_app || index < 0 || index >= g_app->post_count) return NULL;
  return g_app->posts[index];
}

// ============================================================
// app_update_status — refresh main window status bar
// ============================================================

void app_update_status(void) {
  if (!g_app || !g_app->main_win) return;
  char buf[64];
  snprintf(buf, sizeof(buf), "%d post%s",
           g_app->post_count, g_app->post_count == 1 ? "" : "s");
  send_message(g_app->main_win, evStatusBar, 0, buf);
}
