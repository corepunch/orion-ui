# Company server backups

The offline maintenance tool operates on one company directory at a time. Stop
the company server first: backup takes the same exclusive `server.lock` and
fails if a cooperating server is running. Supply a new destination outside the
company directory:

```sh
python3 tools/orion_server_admin.py backup /srv/orion/company /backups/company-2026-11-12
python3 tools/orion_server_admin.py verify /backups/company-2026-11-12
```

The backup contains the SQLite metadata and receipts, current and previous
snapshots, checksums, and the audit-chain head. It deliberately excludes access
tokens, `users.json`, server executable paths and credentials. Copy it to
independent protected storage and retain multiple generations; a checksum in the
same backup is not proof against a party able to modify the whole backup.

### Legacy databases without a company identity

The verifier rejects a database that has no valid company identity. For an old
installation that predates identities, stop the server and preserve an offline
copy of the entire company directory, including any SQLite journal or WAL, in
protected storage before updating. Use a compatible current server package and
start the original company once on loopback, with clients disconnected:

```sh
python3 tools/orion_server.py serve --state /srv/orion/company --bind 127.0.0.1
```

The server creates and persists an identity when it opens the legacy database.
Stop it cleanly, then run `backup` and `verify` as above and test restoration in
an isolated directory. Keep the original offline copy until application records
and the restored company have been reconciled. Do this upgrade only once on the
original company: independently opening multiple legacy copies generates
separate identities. An existing invalid identity is rejected and requires
operator investigation; do not delete or replace it to bypass verification.

Restore only to a new directory and select the trusted executable and resources
for this installation explicitly:

```sh
python3 tools/orion_server_admin.py restore /backups/company-2026-11-12 /srv/orion/recovered \
  --executable /opt/orion/bin/orion-service --resources /opt/orion/share/lodge
python3 tools/orion_server.py user --state /srv/orion/recovered --user operator --role admin
```

The restored company starts with no users; provision new tokens separately.
Validate the application, jurisdiction, installed version and business records
before serving clients. The restoration verifies the backup before publication;
it does not run the chosen business host or prove that the host understands the
restored schema. Test the recovered company in isolation before switching users.
Backups and restores whose estimated local working footprint exceeds 500 MB are
rejected; perform those operations in an appropriate cloud environment instead.

Verification checks SQLite integrity, current snapshot against its SQLite digest,
backup inventory, and the audit hash chain. The server metadata does not retain
an independent digest for the previous snapshot, so verification can detect a
change to it after backup, not one that happened before export. The audit chain
has no external trusted anchor and therefore cannot prove it was not truncated
or rewritten. These checks do not replace independent backups, access control,
an externally retained audit trail or an application-level reconciliation.

The server synchronizes snapshot files, then directories on POSIX, before SQLite
publication with `synchronous=FULL`. Directory synchronization is not implemented
by the Python server on Windows. The current crash tests cover process termination
on macOS, not power loss, failing storage controllers, Windows directory durability
or network filesystems. Do not infer those guarantees from the SQLite setting alone.

## Inspecting access

List active user IDs and roles without exposing tokens or their hashes:

```sh
python3 tools/orion_server.py users --state /srv/orion/company
```

To rotate a token, run `user` again for that ID and role; distribute the new
secret securely. To revoke it, run:

```sh
python3 tools/orion_server.py user --state /srv/orion/company --user ID --role ROLE --revoke
```

The list shows current accounts, not a history of revoked accounts. Manage this
local operator command through OS permissions.
If the user directory is malformed or contains duplicate IDs or token hashes,
authentication and account changes fail closed. Restore a known-good
`users.json` from a secure administrative copy or recreate the directory and
issue new tokens; company backups intentionally exclude credentials. Never
publish the directory or its token hashes in a diagnostic report.

## Remote client request recovery

The shared desktop clients save the exact pending request before transmission,
using the OS user’s protected settings directory. The outbox is bound to the
application, configured server origin, server-assigned company identity and
authenticated actor. Only a confirmed result clears that request; after a
connection loss or restart, reconnect with the same account and server and
retry it unchanged. A different account, endpoint or company does not replay it.
An old pending request may require returning to its original connection or
operator reconciliation. Never assume a timeout means the server did not commit.

Outbox files have owner-only directory/file permissions and contain business
request data but no bearer token. They are not encrypted; protect the OS user
account and storage accordingly. Filesystem durability and locks require a
reliable local filesystem. Restoring a server backup preserves its company ID;
do not run the original and restored copy concurrently as independent servers.
For isolated testing only, trusted host launchers may set `ORION_REMOTE_PROFILE`
to an existing directory owned by the user with mode 0700; symlink final
components and unsafe permissions are rejected. Lua code receives no profile
path API. Remote application and plugin Lua code is trusted: code able to load
the native remote module can inspect or clear its pending request. Do not load
unreviewed plugins into a remote workspace. Windows shared desktop
transport/outbox is not yet implemented.
