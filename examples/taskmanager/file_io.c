// File I/O: binary save/load for the task list.
//
// File format:
//   [file_header_t]
//   [task_entry_t] x task_count
//   [string data block: title\0 desc\0 for each task in order]

#include "taskmanager.h"

// ============================================================
// Binary structures
// ============================================================

#define TASK_FILE_MAGIC   0x5441534Bu  /* 'TASK' */
#define TASK_FILE_VERSION 1u

#pragma pack(push, 1)
typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t task_count;
  uint32_t reserved;
} file_header_t;

typedef struct {
  int32_t  id;
  int32_t  priority;
  int32_t  status;
  uint32_t created_date;
  uint32_t due_date;
  uint32_t title_offset;
  uint32_t title_len;
  uint32_t desc_offset;
  uint32_t desc_len;
} task_entry_t;
#pragma pack(pop)

// ============================================================
// task_file_save
// ============================================================

bool task_file_save(const char *path, task_doc_t *doc) {
  if (!path || !doc) return false;

  FILE *f = fopen(path, "wb");
  if (!f) return false;

  // Write placeholder header (we will fill it in below).
  file_header_t hdr;
  hdr.magic      = TASK_FILE_MAGIC;
  hdr.version    = TASK_FILE_VERSION;
  hdr.task_count = (uint32_t)doc->task_count;
  hdr.reserved   = 0;
  if (fwrite(&hdr, sizeof(hdr), 1, f) != 1) { fclose(f); return false; }

  // Compute string offsets and write task entries.
  uint32_t str_off = 0;
  for (int i = 0; i < doc->task_count; i++) {
    task_t *t = doc->tasks[i];
    uint32_t tlen = t->title       ? (uint32_t)strlen(t->title)       : 0;
    uint32_t dlen = t->description ? (uint32_t)strlen(t->description) : 0;

    task_entry_t e;
    e.id           = (int32_t)t->id;
    e.priority     = (int32_t)t->priority;
    e.status       = (int32_t)t->status;
    e.created_date = t->created_date;
    e.due_date     = t->due_date;
    e.title_offset = str_off;
    e.title_len    = tlen;
    str_off       += tlen + 1;
    e.desc_offset  = str_off;
    e.desc_len     = dlen;
    str_off       += dlen + 1;

    if (fwrite(&e, sizeof(e), 1, f) != 1) { fclose(f); return false; }
  }

  // Write string block — check each write for I/O errors.
  for (int i = 0; i < doc->task_count; i++) {
    task_t *t = doc->tasks[i];
    const char *title = t->title       ? t->title       : "";
    const char *desc  = t->description ? t->description : "";
    if (fwrite(title, strlen(title) + 1, 1, f) != 1) { fclose(f); return false; }
    if (fwrite(desc,  strlen(desc)  + 1, 1, f) != 1) { fclose(f); return false; }
  }

  if (fclose(f) != 0) return false;
  return true;
}

// Append a task directly to the array (for use during load only).
// Unlike app_add_task(), this does NOT assign IDs or set modified.
static bool file_load_append(task_doc_t *doc, task_t *task) {
  if (doc->task_count >= doc->task_capacity) {
    int new_cap = doc->task_capacity * 2;
    task_t **newbuf = (task_t **)realloc(doc->tasks,
                                          (size_t)new_cap * sizeof(task_t *));
    if (!newbuf) return false;
    doc->tasks         = newbuf;
    doc->task_capacity = new_cap;
  }
  doc->tasks[doc->task_count++] = task;
  return true;
}

// ============================================================
// task_file_load
// ============================================================

bool task_file_load(const char *path, task_doc_t *doc) {
  if (!path || !doc) return false;

  FILE *f = fopen(path, "rb");
  if (!f) return false;

  // Read and validate header.
  file_header_t hdr;
  if (fread(&hdr, sizeof(hdr), 1, f) != 1) { fclose(f); return false; }
  if (hdr.magic != TASK_FILE_MAGIC || hdr.version != TASK_FILE_VERSION) {
    fclose(f); return false;
  }

  uint32_t count = hdr.task_count;

  // Read task entries.
  task_entry_t *entries = (task_entry_t *)malloc(count * sizeof(task_entry_t));
  if (!entries) { fclose(f); return false; }
  if (count > 0 && fread(entries, sizeof(task_entry_t), count, f) != count) {
    free(entries); fclose(f); return false;
  }

  // Determine string block size: read to EOF.
  long str_start = ftell(f);
  fseek(f, 0, SEEK_END);
  long file_end = ftell(f);
  long str_size = file_end - str_start;
  if (str_size < 0) str_size = 0;

  char *strbuf = (char *)malloc((size_t)str_size + 1);
  if (!strbuf) { free(entries); fclose(f); return false; }
  strbuf[str_size] = '\0';

  fseek(f, str_start, SEEK_SET);
  if (str_size > 0 && (long)fread(strbuf, 1, (size_t)str_size, f) != str_size) {
    free(strbuf); free(entries); fclose(f); return false;
  }
  fclose(f);

  // Clear existing tasks.
  for (int i = 0; i < doc->task_count; i++)
    task_free(doc->tasks[i]);
  doc->task_count = 0;
  doc->next_id    = 1;
  doc->selected_idx = -1;

  // Rebuild tasks from the loaded data.
  size_t strbuf_size = (size_t)str_size;
  int max_id = 0;
  for (uint32_t i = 0; i < count; i++) {
    task_entry_t *e = &entries[i];

    // Validate and copy title — bounds check prevents out-of-bounds reads
    // on malformed/corrupted files.
    size_t tlen = (size_t)e->title_len;
    size_t dlen = (size_t)e->desc_len;

    bool title_ok = ((size_t)e->title_offset <= strbuf_size &&
                     tlen <= strbuf_size - (size_t)e->title_offset);
    bool desc_ok  = ((size_t)e->desc_offset  <= strbuf_size &&
                     dlen <= strbuf_size - (size_t)e->desc_offset);

    char *title_buf = (char *)malloc(title_ok ? tlen + 1 : 1);
    char *desc_buf  = (char *)malloc(desc_ok  ? dlen + 1 : 1);
    if (!title_buf || !desc_buf) { free(title_buf); free(desc_buf); continue; }

    if (title_ok && tlen > 0)
      memcpy(title_buf, strbuf + e->title_offset, tlen);
    title_buf[title_ok ? tlen : 0] = '\0';

    if (desc_ok && dlen > 0)
      memcpy(desc_buf, strbuf + e->desc_offset, dlen);
    desc_buf[desc_ok ? dlen : 0] = '\0';

    task_t *t = task_create(title_buf, desc_buf,
                            (task_priority_t)e->priority,
                            (task_status_t)e->status,
                            e->due_date);
    free(title_buf);
    free(desc_buf);
    if (!t) continue;

    // Preserve the persisted ID and created date.
    t->id           = (int)e->id;
    t->created_date = e->created_date;

    if (file_load_append(doc, t)) {
      if ((int)e->id > max_id) max_id = (int)e->id;
    } else {
      task_free(t);
    }
  }

  // Ensure next_id is above all loaded IDs.
  if (max_id >= doc->next_id) doc->next_id = max_id + 1;

  free(strbuf);
  free(entries);
  return true;
}
