// MODEL: Post and comment data structures and CRUD operations.

#include "socialfeed.h"

// ============================================================
// Portable string-duplicate helper
// ============================================================

char *sf_strdup(const char *s) {
  if (!s) s = "";
  size_t len = strlen(s);
  char *copy = (char *)malloc(len + 1);
  if (copy) memcpy(copy, s, len + 1);
  return copy;
}

// ============================================================
// comment_create / comment_free
// ============================================================

comment_t *comment_create(const char *author, const char *text) {
  comment_t *c = (comment_t *)calloc(1, sizeof(comment_t));
  if (!c) return NULL;
  c->author = sf_strdup(author);
  c->text   = sf_strdup(text);
  if (!c->author || !c->text) {
    comment_free(c);
    return NULL;
  }
  c->created_at = (uint32_t)time(NULL);
  return c;
}

void comment_free(comment_t *c) {
  if (!c) return;
  for (int i = 0; i < c->reply_count; i++)
    comment_free(c->replies[i]);
  free(c->replies);
  free(c->author);
  free(c->text);
  free(c);
}

// ============================================================
// comment_add_reply — append a reply to a comment
// ============================================================

bool comment_add_reply(comment_t *c, comment_t *reply) {
  if (!c || !reply) return false;
  if (c->reply_count >= c->reply_cap) {
    int newcap = c->reply_cap ? c->reply_cap * 2 : REPLIES_INIT_CAP;
    comment_t **newbuf = (comment_t **)realloc(c->replies,
                                               (size_t)newcap * sizeof(comment_t *));
    if (!newbuf) return false;
    c->replies  = newbuf;
    c->reply_cap = newcap;
  }
  c->replies[c->reply_count++] = reply;
  return true;
}

// ============================================================
// comment_like — increment the like counter
// ============================================================

void comment_like(comment_t *c) {
  if (c) c->like_count++;
}

// ============================================================
// post_create / post_free
// ============================================================

post_t *post_create(const char *author, const char *title, const char *body) {
  post_t *p = (post_t *)calloc(1, sizeof(post_t));
  if (!p) return NULL;
  p->author = sf_strdup(author);
  p->title  = sf_strdup(title);
  p->body   = sf_strdup(body);
  if (!p->author || !p->title || !p->body) {
    post_free(p);
    return NULL;
  }
  p->created_at = (uint32_t)time(NULL);
  return p;
}

void post_free(post_t *p) {
  if (!p) return;
  for (int i = 0; i < p->comment_count; i++)
    comment_free(p->comments[i]);
  free(p->comments);
  free(p->author);
  free(p->title);
  free(p->body);
  free(p);
}

// ============================================================
// post_add_comment — append a comment, grow the array if needed
// ============================================================

bool post_add_comment(post_t *p, comment_t *c) {
  if (!p || !c) return false;
  if (p->comment_count >= p->comment_cap) {
    int newcap = p->comment_cap ? p->comment_cap * 2 : COMMENTS_INIT_CAP;
    comment_t **newbuf = (comment_t **)realloc(p->comments,
                                               (size_t)newcap * sizeof(comment_t *));
    if (!newbuf) return false;
    p->comments   = newbuf;
    p->comment_cap = newcap;
  }
  p->comments[p->comment_count++] = c;
  return true;
}

// ============================================================
// post_like — increment the like counter
// ============================================================

void post_like(post_t *p) {
  if (p) p->like_count++;
}
