// Database view over the songs saved in Groove's own storage: one row per
// .groove file in db->source_path, newest first. dbLoad rescans the folder;
// dbDelete removes the file behind a row.
#include "groove.h"

#define GR_MAX_SONGS 512

typedef struct { song_entry_t rows[GR_MAX_SONGS]; int count; } songs_ctx_t;

static const db_field_msg_binding_t kSongBindings[] = {
  { "id", 0x7200 }, { "name", 0x7201 }, { "path", 0x7202 },
  { "modified", 0x7203 }, { "bpm", 0x7204 }, { "bars", 0x7205 },
};
static const db_field_schema_t kSongSchema[] = {
  { "id",       DB_TYPE_INT,    0,   true,  NULL, NULL },
  { "name",     DB_TYPE_STRING, 128, false, NULL, NULL },
  { "path",     DB_TYPE_STRING, 512, false, NULL, NULL },
  { "modified", DB_TYPE_INT,    0,   false, NULL, NULL },
  { "bpm",      DB_TYPE_INT,    0,   false, NULL, NULL },
  { "bars",     DB_TYPE_INT,    0,   false, NULL, NULL },
};
static db_table_schema_t kSongTables[] = {
  { TABLE_SONGS, "songs", "song_entry_t", kSongSchema, ARRAY_LEN(kSongSchema), NULL, 0 },
};
static db_schema_def_t kSongDbSchema = {
  .name = "songs", .class_name = "groove_songs_db", .tables = kSongTables, .table_count = ARRAY_LEN(kSongTables),
};

typedef struct { database_t *db; songs_ctx_t *ctx; } scan_t;

static bool_t scan_entry(AXdirent const *e, void *userdata) {
  scan_t *scan = userdata;
  songs_ctx_t *ctx = scan->ctx;
  size_t n = strlen(e->name);
  if (e->is_directory || e->is_hidden || n <= 7 || strcasecmp(e->name + n - 7, ".groove")) return TRUE;
  if (ctx->count == GR_MAX_SONGS) {
    fprintf(stderr, "[songs-db] scan stopped dir=%s limit=%d\n", scan->db->source_path, GR_MAX_SONGS);
    fflush(stderr);
    return FALSE;
  }
  song_entry_t *row = &ctx->rows[ctx->count];
  memset(row, 0, sizeof(*row));
  if (snprintf(row->path, sizeof(row->path), "%s/%s", scan->db->source_path, e->name) >= (int)sizeof(row->path)) return TRUE;
  song_name_from_path(e->name, row->name, sizeof(row->name));
  row->modified = (int)e->modified;
  song_t *song = malloc(sizeof(*song));
  if (song && song_read(row->path, song)) {   // bpm 0 marks a file Groove cannot read
    row->bpm = song->bpm;
    row->bars = (song_length_ticks(song) + GR_TICKS_BAR - 1) / GR_TICKS_BAR;
  }
  free(song);
  ctx->count++;
  return TRUE;
}

static int newest_first(const void *pa, const void *pb) {
  const song_entry_t *a = pa, *b = pb;
  if (a->modified != b->modified) return a->modified < b->modified ? 1 : -1;
  return strcasecmp(a->name, b->name);
}

static bool songs_scan(database_t *db, songs_ctx_t *ctx) {
  scan_t scan = { db, ctx };
  ctx->count = 0;
  if (!db->source_path[0] || !axListDir(db->source_path, scan_entry, &scan)) return false;
  qsort(ctx->rows, (size_t)ctx->count, sizeof(ctx->rows[0]), newest_first);
  for (int i = 0; i < ctx->count; i++) ctx->rows[i].id = i + 1;
  return true;
}

static result_node_t *songs_fetch(const songs_ctx_t *ctx) {
  result_node_t *head = NULL, **tail = &head;
  for (int i = 0; i < ctx->count; i++) {
    result_node_t *node = calloc(1, sizeof(*node) + sizeof(song_entry_t));
    if (!node) {
      fprintf(stderr, "[songs-db] fetch allocation failed row=%d count=%d\n", i, ctx->count);
      fflush(stderr);
      free_result_list(head);
      return NULL;
    }
    memcpy(node->data, &ctx->rows[i], sizeof(song_entry_t));
    *tail = node;
    tail = (result_node_t **)&node->next;
  }
  return head;
}

static bool songs_db_delete(database_t *db, songs_ctx_t *ctx, int id) {
  int i = 0;
  while (i < ctx->count && ctx->rows[i].id != id) i++;
  if (i == ctx->count || remove(ctx->rows[i].path) != 0) {
    fprintf(stderr, "[songs-db] delete rejected dir=%s id=%d count=%d path=%s\n", db->source_path, id, ctx->count,
            i < ctx->count ? ctx->rows[i].path : "<none>");
    fflush(stderr);
    return false;
  }
  memmove(&ctx->rows[i], &ctx->rows[i + 1], (size_t)(ctx->count - i - 1) * sizeof(ctx->rows[0]));
  ctx->count--;
  return true;
}

static result_t song_object_proc(const void *object, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg != dbObjGetFieldText || !object || !lparam || !HIWORD(wparam)) return false;
  const song_entry_t *row = object;
  char *out = lparam;
  size_t cap = HIWORD(wparam);
  switch (LOWORD(wparam)) {
    case 0x7200: snprintf(out, cap, "%d", row->id);       return true;
    case 0x7201: snprintf(out, cap, "%s", row->name);     return true;
    case 0x7202: snprintf(out, cap, "%s", row->path);     return true;
    case 0x7203: snprintf(out, cap, "%d", row->modified); return true;
    case 0x7204: snprintf(out, cap, "%d", row->bpm);      return true;
    case 0x7205: snprintf(out, cap, "%d", row->bars);     return true;
    default: return false;
  }
}

lresult_t groove_songs_db(database_t *db, uint32_t msg, uint32_t wparam, void *lparam) {
  songs_ctx_t *ctx = db ? db->userdata : NULL;
  switch (msg) {
    case dbCreate:
      if (!db || !(db->userdata = calloc(1, sizeof(songs_ctx_t)))) {
        fprintf(stderr, "[songs-db] create failed db=%p dir=%s\n", (void *)db, db ? db->source_path : "<none>");
        fflush(stderr);
        return 0;
      }
      return 1;
    case dbLoad:
      if (!ctx || !songs_scan(db, ctx)) {
        fprintf(stderr, "[songs-db] load failed dir=%s\n", db ? db->source_path : "<none>");
        fflush(stderr);
        return 0;
      }
      return 1;
    case dbSave: return 1;
    case dbDestroy:
      if (db) { free(db->userdata); db->userdata = NULL; }
      return 1;
    case dbGetDirty: return 0;
    case dbFetch:
      if (ctx && LOWORD(wparam) == TABLE_SONGS && HIWORD(wparam) == 0) return (lresult_t)songs_fetch(ctx);
      fprintf(stderr, "[songs-db] dbFetch rejected table=%u filter_field=%u ctx=%p\n",
              (unsigned)LOWORD(wparam), (unsigned)HIWORD(wparam), (void *)ctx);
      fflush(stderr);
      return 0;
    case dbDelete:
      if (ctx && wparam == TABLE_SONGS) return songs_db_delete(db, ctx, (int)(intptr_t)lparam);
      fprintf(stderr, "[songs-db] dbDelete rejected table=%u ctx=%p\n", (unsigned)wparam, (void *)ctx);
      fflush(stderr);
      return 0;
    case dbGetObjectProc:
      return wparam == TABLE_SONGS ? (lresult_t)song_object_proc : 0;
    case dbGetFieldBindings:
      if (lparam) *(int *)lparam = wparam == TABLE_SONGS ? ARRAY_LEN(kSongBindings) : 0;
      return wparam == TABLE_SONGS ? (lresult_t)kSongBindings : 0;
    case dbGetSchema:
      kSongDbSchema.name = db ? db->name : "songs";
      kSongDbSchema.class_name = db ? db->class_name : "groove_songs_db";
      kSongDbSchema.source_path = db ? db->source_path : NULL;
      return (lresult_t)&kSongDbSchema;
    case dbGetFieldMeta:
      if (lparam) *(int *)lparam = wparam == TABLE_SONGS ? ARRAY_LEN(song_entry_t_fields) : 0;
      return wparam == TABLE_SONGS ? (lresult_t)song_entry_t_fields : 0;
    case dbInsert: case dbUpdate:
      fprintf(stderr, "[songs-db] rejected write message=%u table=%u (songs are written by Save Song)\n", (unsigned)msg, (unsigned)wparam);
      fflush(stderr);
      return 0;
    default: return 0;
  }
}
