# Database access boundary

A trusted host can opt a database into process-local access checks with
`database_set_access(db, actor, capabilities, policy, audit, context)`. The
actor is a nonempty stable identity chosen by the host after authentication.
The host must bind the session before exposing the store, serialize sessions
that share a database object, and discard session-owned edit contexts before
switching actors. Changing the actor while a tracked change scope is open is
rejected. This API does not authenticate users, assign roles, encrypt data, or
persist an identity database.

`DB_ACCESS_READ`, `DB_ACCESS_WRITE` and `DB_ACCESS_ADMIN` are independent
capabilities. Record and metadata reads require READ; changes, transaction
control and save require WRITE; loading and unrecognized custom messages require
ADMIN. Create and destroy are internal lifecycle operations. A host may further
restrict a capability by providing a policy callback, which receives the
actor, message and `wparam` (the table id is its low word on table messages).
The policy cannot grant a missing capability. A rejected dispatch returns zero,
sets a localized error and logs the identity, database and message to stderr.
The same dispatch gate is used by native `send_db_message` calls and Lua's
database bindings; a Lua script cannot assign a database session. Editing
contexts inherit the store's access configuration and save through the store's
checks. Secured XML cascaded deletes use a nested change scope and roll back if
a child delete is rejected.

The optional audit callback reports denied attempts and the immediate outcomes
of writes, commits, reverts and custom messages. These are **attempt events**:
a successful write can later be rolled back and callbacks are neither durable
nor atomic with the database. They are useful for telemetry, not a compliant
accounting or security audit journal. An authoritative audit requires a
transactional, append-only store with stable actor identity and protected
retention; the caller must not claim that the callback supplies one.

The gate is opt-in and cannot sandbox native code in the same process. Existing
record APIs expose mutable pointers, native code can call a database procedure
directly or reconfigure a session, and process owners can modify XML files.
Do not expose a native process or its pointers to untrusted clients. A shared
server must authenticate each request, bind a host-created session, authorize
all operations and use a trusted transaction boundary; local file locks alone
do not provide multi-user concurrency or protection against non-cooperating
writers.
