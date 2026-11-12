#ifndef __ORION_REMOTE_OUTBOX_H__
#define __ORION_REMOTE_OUTBOX_H__

enum { REMOTE_OUTBOX_OK, REMOTE_OUTBOX_EMPTY, REMOTE_OUTBOX_CONFLICT, REMOTE_OUTBOX_ERROR };
/* Trusted host only; no storage paths or bearer tokens are exposed to Lua. */
int remote_outbox_save(const char *app, const char *origin, const char *company, const char *actor, const char *request);
int remote_outbox_load(const char *app, const char *origin, const char *company, const char *actor, char **request);
int remote_outbox_clear(const char *app, const char *origin, const char *company, const char *actor, const char *expected);
/* Explicit base is for trusted platform tests and embedding hosts. */
int remote_outbox_save_at(const char *base, const char *app, const char *origin, const char *company, const char *actor, const char *request);
int remote_outbox_load_at(const char *base, const char *app, const char *origin, const char *company, const char *actor, char **request);
int remote_outbox_clear_at(const char *base, const char *app, const char *origin, const char *company, const char *actor, const char *expected);
#endif
