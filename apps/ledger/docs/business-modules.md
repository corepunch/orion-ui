# Ledger business capability manifests

Ledger is still a separately installed accounting application. The operator's
national configuration (for example, `ru-2026` or `de-2026`) is independent
from the service capability profile. Account plans, numbering, tax and posting
rules, closing, reversal, document history and precise amount validation stay
with the existing Ledger datasource and native service. No records or
configuration files migrate as a result of this change.

The host's standard profile (`ledger.accounting`, API 1) composes the catalog,
item, counterparty, document, journal and period capabilities. The former
service commands keep their identifiers, input and result shapes. Query the
enabled commands with:

```json
{"command":"capabilities.list","actor":"local-audit","role":"auditor","args":{}}
```

The successful result contains `api`, `profile`, a dependency-ordered
`modules` array with declared resources, and a sorted `commands` array. Each
command has its identifier, owning module and `read` or `write` mode. Commands
the actor may not invoke are omitted. Discovery itself is built into the service
and does not need a module. It is also available through the existing Ledger
company service, whose independent command and role allowlist remains in force.

## Adding a shipped capability

A trusted file under `share/business` returns a factory given the host's
capabilities and policy. The factory declares:

- `id`: stable two-part module name, such as `ledger.documents`.
- `api`: supported manifest version, currently `1`.
- `dependencies`: stable module IDs resolved before this module. Missing IDs,
  duplicate manifests and dependency cycles fail assembly.
- `owns`: logical resource names such as `ib.documents`. Two enabled modules
  cannot declare the same resource. This is metadata, not a database ACL.
- `commands`: entries with `id`, `mode`, `roles` and `run(args)`. A command
  must use its module's namespace. Duplicate commands and an auditor write
  grant are rejected. The host checks the role before calling any handler.

The host registers known shipped sources and selects modules explicitly.
Requests are data only and cannot select a profile or load code. As before,
the process invoking `ledger-run --request` must authenticate its actor and
supply the appropriate role, and writes additionally need `--write`. The
native service owns transactions; modules call that boundary rather than
changing accounting tables directly. Do not execute an external module in this
trusted Lua state based on an incoming request. This registry is not a Lua
sandbox, user/package manager, or an authenticated multi-tenant deployment.

This is a service composition seam, not a rewrite of Ledger's desktop UI.
Industry workflows can call the explicit service contract without recreating
postings in Lua. App installation, third-party distribution, migration
contributions, navigation contributions, LAN identity, a new local command-deduplication store, and an event/outbox pipeline are not
supplied here. Existing company-service retry behavior is unchanged.

## Verify

```sh
lua5.4 apps/ledger/tests/business_modules_test.lua
make build/bin/test_ledger_test
build/bin/test_ledger_test
```

The first command exercises dependency ordering, role gates, JSON discovery
and real request dispatch against a fixture. Native tests separately exercise
transactions, precise money, posting, reversal and the service's audit history.

`stock.balance` is a read-only company command for the standard Ledger roles,
requiring an item ID and a warehouse ID (zero for legacy/unassigned stock).
Balances are decimal-string thousandths. Transfers, holds and posting stay
behind the [native warehouse API](warehouse-stock.md); no marketplace, workshop
or remote stock write is implied by discovering this capability.
