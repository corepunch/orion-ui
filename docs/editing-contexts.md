---
layout: default
title: Editing Contexts (design)
nav_exclude: true
---

# Editing contexts: design note

**Status: proposal.** Nothing here is implemented yet. The note says what is wrong
with change scopes today and proposes editing contexts after EOF's
`EOEditingContext`, so that the work can be reviewed before it starts.

## How change scopes work today

`db_schema_xml` keeps one stack of change scopes per database
(`orion/user/db_schema_xml.c`, `dbs_begin` / `dbs_end` / `dbs_restore`):

- `dbBeginChanges` snapshots **every stored record of every table** and pushes the
  snapshot.
- Writes go straight into the shared tables, as they do without a scope.
- `dbCommitChanges` pops the innermost snapshot and frees it.
- `dbRevertChanges` pops it and restores every table to it. Surviving records keep
  their addresses.

Everything that edits uses this stack:

| User | Code | Use of the stack |
|---|---|---|
| Database dialogs | `show_db_dialog_opts()`, `orion/commdlg/dialog.c` | Begin when the dialog opens (a new record is inserted at once), commit on OK, revert on Cancel. Nested dialogs nest scopes. |
| Lua add-ons | `orion.db.begin/commit/revert`, `Model:transaction` | A scope around the function |
| Ledger automation | `ledger.transaction`, `ledger_script_run()` | A scope per run; a read-only run is a scope that is always reverted |
| Ledger 1C import | `ledger_import_enterprisedata()` | One scope for the whole import |

## What is wrong with it

1. **A scope belongs to nobody.** Commit and revert act on the innermost scope,
   whoever opened it. That is correct only while editors open and close in strict
   nesting. Add-on and automation scopes last for one synchronous call, so they
   always nest. A database dialog keeps its scope open across user interaction,
   and it nests only because it is modal. Two document dialogs open side by side
   would break it: OK in the first commits the second's scope, and Cancel in the
   second reverts the first's.
2. **Revert restores the whole database.** It also undoes changes made by an
   overlapping editor after the scope opened. Those changes are not just lost:
   records inserted meanwhile are freed while their editor may still hold them.
3. **No isolation.** A dialog writes into the shared tables as the user types. It
   inserts a new document as soon as it opens, before the user has decided
   anything, and every reader sees that. Today nothing else reads while a modal
   dialog is open. A journal refreshing beside an open document would show the
   half-edited one.
4. **Cost grows with the infobase.** A scope copies every stored record, even
   when editing one document. Every document dialog pays this cost, and it grows
   linearly with the infobase.
5. **Views are told nothing.** Views learn about changes only through explicit calls
   (`after_save()` → `ledger_refresh_views()`, `tvnChanged`). Each new editor has to
   remember them.

Points 1 to 3 are latent: they bite only when an editor that keeps its scope
across user interaction stops being modal. Point 4 costs every document dialog
today, and it grows with the infobase.

## Proposal: editing contexts

After EOF (`EOEditingContext`, `EOObjectStore`):

- **The database is the store.** It holds the committed records, as today.
- **An editing context** is owned by one editor: a dialog, an add-on page, a script
  run or an import. It records that editor's changes as an overlay of inserted,
  updated and deleted records over its parent. The parent is the store or another
  context.
- **Reads through a context** see its overlay first, then its parent. Writes through
  a context touch only the overlay (copy-on-write of the record).
- **`save`** pushes the overlay to the parent. A child context's save is how a
  nested dialog's OK works today. A top-level save applies the changes to the
  store through the class proc's `dbInsert` / `dbUpdate` / `dbDelete`, all or
  nothing.
- **`revert`** drops the overlay: O(changes), not O(database), and nobody else is
  affected.
- **The store announces each save** (`dbnChanged`: table and keys) to views and
  other contexts. Views refresh; a context holding the same record can refresh it
  or report a conflict.

```c
typedef struct db_context_s db_context_t;

db_context_t *db_context_create(database_t *db, db_context_t *parent);  // parent NULL: the store
bool  db_context_save(db_context_t *ctx, char *error, size_t error_sz);   // nothing applied on failure
void  db_context_revert(db_context_t *ctx);
void  db_context_destroy(db_context_t *ctx);                             // unsaved changes are dropped
bool  db_context_has_changes(const db_context_t *ctx);
lresult_t send_context_message(db_context_t *ctx, uint32_t msg, uint32_t wparam, void *lparam);
```

The messages stay the same (`dbFetch`, `dbFind`, `dbInsert`, `dbUpdate`, `dbDelete`,
`dbInitRecord`, `dbCountReferrers`); only their address changes. A message sent to
the `database_t` itself goes to the store, so code that does not edit is
unchanged.

### How today's users map onto it

| Today | With contexts |
|---|---|
| `show_db_dialog_opts()` begin / commit / revert | The dialog owns a context, a child of its parent dialog's or of the store. OK saves it, Cancel destroys it. A detail TableView reads through the dialog's context. |
| `Model:transaction(fn)` | A child of the page's context, saved when `fn` returns and dropped when it raises |
| An add-on page | Owns a context; closing without saving drops it |
| `ledger_script_run(read-only)` | A context that is never saved |
| 1C import | A context saved once at the end |
| `dbBeginChanges` and friends | Kept for one release as wrappers over an implicit per-database context stack, then removed |

## Questions to settle before coding

1. **Datasource rules inside a context.** `ledger_ib` numbers a document on insert and
   recalculates it on every line write, and the open dialog shows those totals.
   Class procs therefore have to run against the context, reading and writing its
   overlay, not the store. Proposal: the class proc receives the context as its
   database handle. Document numbers are assigned at the top-level save, so two
   drafts never take the same number.
2. **Record keys.** Inserting in a context needs a key that child records can refer
   to. Two options:
   - take ids from the store at insert time. This is simple; a revert leaves a gap,
     which is harmless because keys are never shown;
   - EOF's temporary global ids, remapped at save.

   Proposal: the former.
3. **Conflicts.** Two contexts may change the same record. Proposal: optimistic
   locking. A context remembers each record as it first read it; at save, a record
   that changed in the store since then refuses the save with "The record was
   changed elsewhere", and nothing is applied.
4. **Pointers.** Callers keep record pointers today, valid until the record is
   deleted. Proposal: a pointer read through a context is valid in that context
   until its next save or revert; a pointer read from the store keeps today's
   guarantee.
5. **Posting.** Posting writes entries, VAT book rows and closing rows. It runs inside
   the posting editor's context, so an unsaved posting never reaches the reports,
   which read the store. Transient tables (report results) stay store-only.

## Plan

1. **Tests that pin down today's behaviour:**
   - two overlapping scopes closed out of order, the case non-modal editors would
     create;
   - a revert erasing an overlapping editor's insert;
   - scope snapshot cost on representative infobases.
2. **Contexts in `db_schema_xml`:**
   - overlay reads and writes, save and revert, nested contexts, conflict detection;
   - `dbBeginChanges` reimplemented on top of them;
   - tests for two contexts editing the same record and for a child saving into its
     parent.
3. **Migrate the users:** database dialogs, `orion.db` / `orion.model`, script pages,
   Ledger's automation and import.
4. **Change notifications** to TableViews, replacing the manual refresh calls they
   make redundant.
5. **Remove the scope stack.**

This is what would make non-modal editors safe, such as document tabs, MDI windows
or a non-modal add-on. It does not decide whether Ledger should have them; that
stays a product question.
