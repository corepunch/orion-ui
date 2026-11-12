# Schema XML persistence

`db_schema_xml` is a single-writer, local-file store. A writable instance takes
an exclusive, non-blocking advisory lock on `<database>.lock` before loading and
keeps it until close. Lock files are stable coordination files and must not be
removed while a store might be open. A second cooperating writer fails to open.
The directory component is canonicalized and symbolic-link or reparse-point
database paths are rejected. This is not a multi-user transaction system: non-cooperating writers,
hard-link aliases and filesystems without reliable advisory locks or durable
rename/fsync semantics are outside the guarantee. On Windows, replacement uses
`MoveFileEx` with write-through; directory fsync is unavailable through this
implementation.

A save writes a temporary file in the same directory, syncs it, preserves the
previous committed file as `<database>.bak`, syncs the directory, atomically
renames the new file over the primary, and syncs the directory again. The
backup is one generation, not an archival backup; copy backups to independent
storage under an operator-managed retention policy. A failure before the final
rename leaves the primary unchanged. Failure after rename, including directory
sync failure, has an uncertain outcome; the caller must inspect the primary and
backup before retrying. A forced process termination releases the lock. Temporary
files left by a terminated process can be removed after confirming the lock is
not held.

To restore, stop other writers and preserve a copy of both files. The explicit
`db_schema_xml_open_backup(name, class_name, schema, path)` operation acquires
the primary lock, checks the backup XML root and supported version, copies and syncs it to a
temporary file, fully loads that copy, then publishes it and returns an open
database. A backup that cannot be loaded leaves the primary and open records
unchanged. A directory sync failure after publication has an uncertain durable
outcome; the open records reflect the published copy. It fails if the backup is
missing, malformed or incompatible. `db_schema_xml_restore_backup(db)` does
the same for an already open, clean store with no active change scope. Recovery
is deliberate; opening a broken primary never silently replaces it. Validation
of business meaning remains the application's responsibility.

Shared templates can be opened with `create_database_with_schema_readonly`.
This rejects write messages; it is not an access-control boundary because the
existing record API exposes mutable pointers. To use a template as a new store,
call `db_schema_xml_seed_to(db, destination)`. It locks a nonexistent destination,
switches to writable mode and marks the store dirty. Save it only after its
initialization succeeds. Do not change `source_path` directly: saving to a path
other than the locked destination is rejected.

## Versions and migrations

A schema may declare `<database version="2" migration="additive">` in an
`.orion` file. Missing version means 1; XML written by the store carries
`orion-version`. A newer file is rejected by an older schema before records are
made available. The additive migration accepts existing tables and fields only
if they remain in the target schema; new fields receive their schema defaults.
It cannot establish that a field's type or business meaning is compatible.
Changing or deleting historical data requires an explicit, reviewed migration.
After migration (or when opening a current-version file), unknown record fields
cause opening to fail without rewriting the source; they are never silently
dropped on the next save. Preserve a copy and use a compatible schema or a
reviewed migration that explicitly handles those fields. Invalid or out-of-range
integer values also reject opening rather than being silently converted; repair
or migrate a preserved copy before retrying.

Native schemas can supply a version and `migrate_xml(root, from_version)`
callback. It receives a libxml root node, performs exactly the step from that
version to the next and returns false on failure. Steps run sequentially in
memory. The callback must not perform external side effects. Failed migration
leaves the source unchanged. On success, publication is deferred until `dbSave`,
so the application can finish model initialization within a change scope first.
The save creates the previous-version backup before replacing the primary.
Read-only templates migrate in memory only; `seed_to` then marks the new store
for publication. On initialization failure, revert the scope and call
`database_discard_on_destroy` before closing, to suppress the usual implicit
save on destruction.

A migrated XML file carries a bounded `orion-history` list of the sequential
schema versions it traversed. The history is validated when opened and written
atomically with the upgraded records. Files written before this feature may
lack history. This is an operational migration record, not a tamper-resistant
or independently retained audit log; it has a fixed 512-byte limit.

The platform does not yet provide compatibility metadata for field types, a
historical schema registry, cross-database atomic transactions,
or a declarative transformation language. Maintain supported-version policies
and test every application migration against actual historical data. The native
`test_db_persistence_test` exercises writer exclusion, backup contents, killed
writer lock release, rejected newer versions, migration and publication failure,
read-only migration, unknown-field rejection, explicit backup recovery and rejection
of a backup with an invalid field before publication. It copies a backup into
a separate directory, restores and reopens it, and checks independent records.
On POSIX, it also kills
a migrating process during each of two migration steps, before saving, and
after a successful save, then checks the primary and previous-version backup.
It does not simulate power failure or termination at every filesystem operation.
