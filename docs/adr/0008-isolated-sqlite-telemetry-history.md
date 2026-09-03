# ADR 0008: Isolated SQLite telemetry history

Status: accepted

## Context

The live telemetry model and Phase 6 SLO window were intentionally in memory.
Operators now need bounded evidence across collector restarts without adding a
remote database dependency, making graph nodes storage clients, or allowing
storage latency to block graph processing. The design must also preserve the
separate service-, graph-, and backend-health meanings established in Phase 6.

## Decision

The telemetry service optionally persists a metadata-only event stream and SLO
evaluations in SQLite. A dedicated worker thread exclusively owns the database;
the main event loop communicates through bounded write and query queues. Writes
use transactional batches, WAL mode and full synchronous durability. Age,
record-count, main-database size, queue count/bytes, batch size, query count,
query limit, query deadline, flush interval, and shutdown deadline are explicit.
Existing rows are pruned before the backend reports ready, maintenance also
runs on a bounded idle interval, and reads apply the current age cutoff so an
expired row is not exposed between physical prune cycles.

Schema version and owning graph ID are stored in the database. Newer schemas and
other graph identities fail closed for history access. The history backend has
an independent degraded state and metrics; its failure does not make the live
telemetry service unready. Ordinary reads reuse the observation authorization
boundary and stable newest-first cursors. Phase 8 control-audit records are
excluded from that boundary and require the dedicated control-audit permission.
There is no history mutation API.

The implementation uses the SQLite module shipped with the Node 22.23.2 Alpine
runtime pinned by immutable digest in both telemetry image stages, so GraphX
adds no package-manager dependency or native add-on lifecycle.

## Consequences

- Telemetry and SLO evidence survives graceful collector restarts.
- Database latency and failure cannot block a graph node or the collector event
  loop; overload is observable as newest-record drops.
- The local backend is simple to deploy and back up, but it is a single-service
  store rather than a replicated or globally queryable platform.
- Main-database growth and retained WAL size are bounded; operators still need
  headroom for an active transaction and filesystem behavior.
- Database files and backups are sensitive operational data and need restricted
  access even though payload bodies and configured GraphX credentials are not stored.
- Remote backends, online backup administration, retention controls, replay,
  deletion, and capture indexing require separate future decisions.
- `node:sqlite` is still marked experimental in the pinned Node 22 runtime. Its
  use is isolated behind the worker/store boundary and covered by image-level
  restart tests; Phase 10 release engineering must either certify the selected
  runtime version or substitute a maintained SQLite adapter behind that seam.
