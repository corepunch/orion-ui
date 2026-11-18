#ifndef __GC_AGENTS_H__
#define __GC_AGENTS_H__

#include "gitclient.h"

#define GC_AGENT_BATCH_SIZE 3
#define GC_AGENT_PROMPT_SIZE 2048

typedef enum { GC_AGENT_CODEX, GC_AGENT_CLAUDE } gc_agent_provider_t;
typedef enum {
  GC_AGENT_PREPARING, GC_AGENT_RUNNING, GC_AGENT_STOPPING, GC_AGENT_STOPPED,
  GC_AGENT_SNAPSHOTTING, GC_AGENT_READY, GC_AGENT_NO_CHANGES, GC_AGENT_FAILED
} gc_agent_phase_t;

typedef struct {
  gc_agent_provider_t provider;
  char prompt[GC_AGENT_PROMPT_SIZE];
} gc_agent_request_t;

typedef struct gc_agent_task_s {
  struct gc_agent_task_s *next;
  int id;
  gc_agent_phase_t phase;
  gc_agent_request_t request;
  char source[512], checkout[512], preview[512], branch[96];
  char base[65], revision[65], target[64];
  const char *error;
  axthread_t worker;
  struct gc_agent_work_s *work;
} gc_agent_task_t;

bool gc_agents_supported(void);
bool gc_agents_start(const char *source, const char *storage, const gc_agent_request_t requests[GC_AGENT_BATCH_SIZE]);
const gc_agent_task_t *gc_agents_tasks(void);
const char *gc_agent_provider_name(gc_agent_provider_t provider);
bool gc_agents_poll(uint32_t timer);
bool gc_agent_stop(int id);
bool gc_agent_output(int id);
bool gc_agent_has_output(const gc_agent_task_t *task);
bool gc_agent_diff(int id, char *out, size_t cap);
bool gc_agent_launch(int id);
void gc_agents_shutdown(void);

result_t gc_agents_page_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
database_t *gc_agents_db_create(void);
bool gc_agents_page_command(uint32_t wparam, void *lparam);
void gc_agents_refresh(void);
void gc_agents_new_batch(void);
void gc_agents_action(uint16_t id);

#endif
