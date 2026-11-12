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

Restore only to a new directory and select the trusted executable and resources
for this installation explicitly:

```sh
python3 tools/orion_server_admin.py restore /backups/company-2026-11-12 /srv/orion/recovered \
  --executable /opt/orion/bin/orion-app --resources /opt/orion/share/lodge
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
