# Durable telemetry history

Phase 7 adds an optional, local SQLite history backend to the telemetry service.
It retains bounded operational telemetry and one-second SLO evaluations across
collector restarts. History is disabled by default, does not change the GraphX
wire protocol, and never participates in the graph data path.

## Architecture and failure isolation

The telemetry event loop validates each UDP event, updates the live in-memory
model, and offers a small metadata-only record to a bounded history queue. A
dedicated Node worker is the sole owner of SQLite. It batches writes, applies
retention, checkpoints its WAL, and answers bounded read queries. No SQLite
operation runs on a graph node or on the telemetry service's main event loop.

History has its own `starting`, `ready`, `degraded`, and `closed` states.
`/api/ready` deliberately remains independent: an unavailable history database
must not restart the collector or interrupt live telemetry. Operators can alert
on `graphx_history_enabled == 1 and graphx_history_backend_up == 0`. A full queue
drops the newest history record and advances the `dropped` counter; it does not
block UDP processing.

Records contain graph, event, node and edge identity plus bounded operational
metadata such as sequence, trace/message identifiers, latency, wire bytes and
CPU percentage. Application message bodies, bearer tokens, HMAC secrets, OTLP
credentials, control credentials, and capture contents are not stored.

## Enabling the backend

The checked-in Compose overlay mounts a named volume into the otherwise
read-only telemetry container:

```sh
docker compose -f compose.yaml -f compose.history.yaml up -d --build
```

The equivalent authoritative configuration is:

```yaml
observability:
  history:
    enabled: true
    backend: sqlite
    database_file: .graphx/history.sqlite
    retention_seconds: 604800
    max_records: 100000
    max_database_bytes: 268435456
    queue_capacity: 4096
    max_queue_bytes: 8388608
    batch_size: 100
    flush_interval_ms: 250
    query_limit: 200
    query_timeout_ms: 2000
    max_pending_queries: 16
    shutdown_timeout_ms: 2000
```

Every property also has an uppercase `GRAPHX_HISTORY_*` deployment override.
YAML booleans and numbers must use their native scalar types; quoted booleans or
numbers are rejected consistently by the C++ planner and telemetry service.
Unknown history properties and explicit empty YAML backend/database values are
also rejected at both boundaries, so a typo cannot silently select a default
retention policy or storage path.
Environment booleans accept only documented true/false spellings, and numeric
overrides must be unsigned base-10 integer strings. Configuration and
environment values use the same validation ranges. An empty environment
override retains the YAML value. A relative database path is resolved from the
directory containing `GRAPHX_CONFIG`; containers should use the absolute path
from `compose.history.yaml`.

Retention is enforced by both age and record count before an existing database
reports ready, before and after write batches, during bounded idle maintenance,
and at graceful close. Queries also apply the age cutoff directly, so a record
cannot become visible after its retention deadline between maintenance cycles.
Idle maintenance runs at least once per minute and more frequently when the
configured retention period is short. SQLite `max_page_count` bounds the main
database below `max_database_bytes`, with room reserved for SQLite metadata.
Startup verifies that SQLite accepted the requested page cap.
If an existing database already exceeds a newly reduced cap, history degrades
with an actionable error instead of silently reporting ready; raise the cap or
restore/compact a suitable database while the service is stopped. WAL writes
are checkpointed after each batch and their retained size is capped, but brief
transactional WAL growth can make total on-disk bytes larger than the main-file
limit. `graphx_history_database_bytes` reports the database, WAL and
shared-memory files together. Operators must provision space for the configured
main-file limit plus a bounded active batch and WAL.

## Read API and console

`GET /api/history/status` returns backend state, schema version, current queue
usage, database bytes, outcome counters, last write time, and a bounded error.
`GET /api/history` returns newest-first records and a stable `nextCursor`.
Both endpoints require `GRAPHX_OBSERVATION_TOKEN` when it is configured.

Supported query parameters are `cursor`, `after`, `before`, `limit`, `node`,
`edge`, `kind`, and `event`. Times are Unix milliseconds. Unknown parameters,
invalid identifiers, filters outside the configured topology, and limits above
`query_limit` return HTTP 400. Query-capacity exhaustion returns 429; a disabled,
starting, or degraded backend returns 503. The API supports reads only—deletion,
reconfiguration, replay, and runtime control are not Phase 7 capabilities.

The console's **History** view refreshes the newest page every five seconds and
can page backward. Loading an older page pauses automatic refresh so the page
chain remains stable; **Return to newest** resumes polling. Overlapping replies
are generation-checked and pages are deduplicated by record ID. The view shows
backend health and queue/write/drop counters. The same observation credential
used for live topology protects history reads.

## Durability, schema, backup, and restore

Schema version 1 uses WAL journaling, `synchronous=FULL`, transactional batches,
indexed time/node/edge lookup, and an incremental-vacuum retention policy. The
database records its owning graph ID. A database owned by another graph or with
a newer schema is rejected and history becomes degraded. The service will not
silently rewrite, downgrade, or mix these databases.

SIGINT/SIGTERM stops accepting new history work and applies one absolute
`shutdown_timeout_ms` deadline to queue drain, any in-flight SQLite lock wait,
checkpoint, close, and worker termination. A missed deadline is logged and the
service exits nonzero; it does not wait for SQLite's normal busy timeout beyond
the configured shutdown budget. For a simple consistent backup, gracefully
stop the telemetry service and copy the database file after shutdown. Restore
it only for the same graph ID, with the service stopped, and preserve
restrictive file ownership and permissions. Online backup and migration
administration are intentionally not exposed as HTTP controls.

The database is operationally sensitive even though payload bodies and secrets
are excluded. Protect the volume, backups, and host directory with the same
access controls as observation telemetry.

## Verification

Unit and subprocess integration tests cover strict scalar/key/path
configuration including full startup rejection, query filters, reduced
age/count policies at reopen, idle expiry and maintenance failure, queue
overflow, restart persistence, observation authentication, incompatible
schemas, graph ownership, and independent service readiness. A mounted console
test covers pagination, paused refresh, and reordered responses. The isolated
container test adds real Compose volume persistence:

```sh
scripts/test-phase7-history.sh
```

The script creates its own Compose project and volume, restarts only its own
telemetry container, verifies the retained event, and removes the isolated
project and volume on exit.

## Implementation references

- [Node.js `node:sqlite` and `DatabaseSync`](https://nodejs.org/api/sqlite.html)
- [SQLite write-ahead logging](https://sqlite.org/wal.html)
- [SQLite PRAGMA reference](https://sqlite.org/pragma.html)
- [SQLite database size limits](https://sqlite.org/limits.html)
