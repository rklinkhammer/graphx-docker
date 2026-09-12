# Telemetry history

The optional history service stores bounded telemetry and control-audit records in a
graph-owned SQLite database. It uses WAL journaling, full synchronization,
transactional batches, retention pruning, record limits, and a configured database
size ceiling.

The worker initializes the current schema for a new database and rejects a database
whose schema version or graph identity does not match the running service. It does
not rewrite an unsupported schema.

`/api/history` and `/api/history/status` require the configured observation token.
History degradation does not stop live telemetry; readiness reports the history
failure independently. Back up the database only while the service is stopped or by
using SQLite's online backup facilities.
