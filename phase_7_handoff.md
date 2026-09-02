# Phase 7 implementation handoff

Date: 2026-09-02

Work package: durable or backend-driven telemetry history

## 1. Outcome summary

Phase 7 is implemented and all three rounds of independent verification
findings have been remediated. GraphX now has an optional SQLite-backed history
service that persists validated operational telemetry and SLO evaluations
across collector/container restarts. The backend is disabled by default and isolated
from graph processing and the telemetry event loop by a dedicated worker and
bounded queues.

The implementation includes strict typed configuration and environment
overrides, schema ownership/version checks, transactional WAL writes, age/count/
database retention bounds, stable cursor queries, observation-authenticated
read APIs, independent backend health and Prometheus alerts, Compose volume
persistence, Grafana and web-console history views, graceful bounded flush, a
live restart regression script, documentation, and ADR 0008.

The remediation makes YAML scalar typing identical across the C++ planner and
Node runtime, rejects an existing database above a reduced configured cap,
enforces shutdown as one absolute deadline even under an external SQLite writer
lock, pins both image stages by immutable digest, and proves the live API path
with an enabled observation token plus a negative unauthenticated request.

| Verification finding | Remediation and regression evidence |
|---|---|
| P2: YAML scalar coercion | YAML history booleans/numbers now require native types in both runtimes; environment parsing is separate and bounded. C++ and Node negative tests reject quoted false/numbers, boolean integers and fractions and cover override precedence. |
| P2: reduced DB cap ignored | Startup compares current pages and verifies SQLite's effective `max_page_count`; an oversized restored/existing DB degrades with current-versus-limit guidance. A normal 4 MiB-to-1 MiB regression passes. |
| P2: shutdown exceeded deadline | SQLite busy waiting is capped by the shutdown budget and `close()` uses one absolute deadline without awaiting worker termination beyond it. A real second-writer lock test returns in about 134 ms at the 100 ms minimum. |
| P3: mutable Node image | Both build stages use the reviewed Node 22.23.2 Alpine digest, recorded in `THIRD_PARTY.md` and ADR 0008. A no-cache image build resolves that digest. |
| P3: false authentication claim | The live Compose test enables an observation token, requires unauthenticated 401, and authenticates status/query calls before and after restart. |
| P2: empty/unknown YAML accepted by Node | The telemetry parser now rejects non-object history configuration, unknown properties, empty YAML backend/path values, and invalid YAML even when an environment override exists. Unit, C++, and full-service startup regressions use the same invalid cases. |
| P3: refresh discarded older console pages | Loading older records now pauses polling until **Return to newest**, merges pages without duplicate IDs, and ignores stale responses by request generation. Pure deterministic web-state tests cover merge and generation behavior. |
| P2: existing/idle DB violated retention | Startup prunes an existing database before reporting ready, bounded idle maintenance runs at least once per minute, and every query applies the current age cutoff. Reopen, idle-prune, non-disclosure, and locked-maintenance degradation regressions pass without long sleeps. |
| P3: helper-only console regression | A mounted JSDOM/React test now exercises the actual component timer, buttons, cursor request, paused polling, token change/unmount, and reordered fetch completion. |

No Phase 8 control-plane or Phase 9 capture/dissector behavior was added. The
pre-existing user edits to `prompt/implement.md` and `prompt/verifier.md` were
preserved and are not implementation output.

## 2. Requirements and acceptance criteria

| Requirement | Measurable acceptance criterion | Result |
|---|---|---|
| Durable history | A committed validated event remains queryable after abrupt process and graceful container restart | Met by subprocess `SIGKILL` and isolated Compose tests |
| Graph isolation | Storage never runs in a graph node or blocks collector event processing | Met: bounded offer plus dedicated SQLite worker |
| Bounded resources | Explicit age, records, DB pages, queue count/bytes, record bytes, batch, query count/limit/deadline, flush and shutdown bounds | Met |
| Failure semantics | Disabled, starting, ready, degraded and closed are distinct; storage failure cannot make service readiness fail | Met, including forced `SQLITE_FULL` and incompatible-schema tests |
| Durable write behavior | Transactional batches, WAL, full synchronous setting and bounded graceful flush | Met |
| Compatibility | Graph configuration version, envelope protocol, live APIs and default deployment behavior remain compatible | Met; history is additive and default-off |
| Security/privacy | Read endpoints reuse observation authorization; secrets, credentials, message payload bodies and captures are not stored | Met |
| Query behavior | Strict filters, topology identity checks, fixed maximum page size and stable newest-first cursor | Met |
| Retention/schema | Age and count pruning; effective main DB cap; schema version and graph ownership fail closed | Met, including larger-to-smaller cap startup |
| Operations | Backend metrics, enabled-aware alert, Grafana panels and GUI status/history view | Met |
| Deployment | Read-only container gains only a dedicated writable named volume through an opt-in overlay | Met |
| Documentation | Data scope, API, limits, failure modes, backup/restore, deployment, tests and decision rationale are current | Met |

## 3. Architecture and compatibility decisions

- Validated UDP telemetry remains best effort. History durably records what the
  collector observed; it is not end-to-end delivery or accounting.
- The main event loop performs only bounded metadata shaping and queue offer.
  `history-worker.mjs` exclusively owns the synchronous SQLite connection.
- The newest record is dropped when the queue count/byte limit is reached.
  Drops and failed writes are exported; the graph and live telemetry continue.
- Write failure transitions history to `degraded`, clears and accounts for work
  that can no longer drain, rejects reads with 503, and leaves `/api/ready`
  independent and healthy.
- SQLite schema version 1 is graph-owned. Newer schemas and another graph's
  database are rejected rather than silently migrated or mixed.
- History reads are additive `GET` endpoints under the existing observation
  credential. No mutation, deletion, replay, reconfiguration, or control API was
  introduced.
- Relative DB paths resolve against the authoritative configuration directory;
  Compose uses an absolute path on its named volume.
- Main DB growth is bounded with a verified `max_page_count`; an existing DB
  above a reduced cap fails history startup. Total disk metrics include DB, WAL
  and SHM, and documentation explicitly requires transient WAL headroom.
- Age/count maintenance runs before backend readiness, around every write batch,
  at graceful close, and on a bounded idle interval. Query-time age filtering
  prevents expired rows from being exposed between physical prune cycles.

ADR 0008 records the choice of an isolated SQLite worker over an in-memory-only
extension, a remote service dependency, or SQLite calls on the event loop.
`node:sqlite` avoids a new native npm dependency but is experimental in the
Node 22.23.2 Alpine runtime pinned by digest. The seam and image-level tests
contain that risk; Phase 10 must certify the selected runtime or replace the
adapter.

## 4. Major components changed

| Component | Evidence |
|---|---|
| Bounded store, config/query validation and metadata shaping | `apps/telemetry/history.mjs` |
| SQLite worker, schema, retention, transactions and cursor query | `apps/telemetry/history-worker.mjs` |
| API, event/SLO ingestion, metrics and shutdown | `apps/telemetry/server.mjs` |
| Unit, negative, failure and subprocess restart tests | `apps/telemetry/history.test.mjs` |
| Typed authoritative configuration and schema | `include/graphx/config.hpp`, `src/config.cpp`, `tests/test_config.cpp`, `config/schema/graphx.schema.json`, `graphx.yaml` |
| Persistent deployment | `compose.history.yaml`, `compose.yaml`, `docker/telemetry.Dockerfile` |
| Operations alert/dashboard | `deploy/observability/alerts.yml`, `deploy/observability/alerts.test.yml`, `deploy/observability/grafana/dashboards/graphx-operations.json` |
| Read-only console view and paging-state tests | `web/src/components/HistoryPanel.jsx`, `web/src/history.mjs`, `web/src/history.test.mjs`, `web/src/App.jsx`, `web/src/styles.css` |
| Live restart acceptance | `scripts/test-phase7-history.sh` |
| Architecture and operating guidance | `docs/adr/0008-isolated-sqlite-telemetry-history.md`, `docs/history.md`, `docs/observability.md`, `docs/security.md`, `docs/test-procedure.md`, `README.md`, `THIRD_PARTY.md` |

## 5. Explicit defaults and limits

| Limit | Default | Accepted range / behavior |
|---|---:|---|
| Retention age | 604,800 s | 60–31,536,000 s |
| Retained records | 100,000 | 10–10,000,000 |
| Idle retention maintenance | At most 60 s | Internal interval is 1–60 s and no greater than half the retention period |
| Main database | 256 MiB | 1 MiB–4 GiB; effective cap verified at startup; oversized existing DB degrades; WAL/SHM and active transaction need headroom |
| Pending writes | 4,096 | 1–65,536; newest is dropped at capacity |
| Pending write bytes | 8 MiB | 64 KiB–64 MiB |
| Encoded record | 16 KiB hard bound | Oversized record is dropped |
| Batch | 100 | 1–1,000 and no greater than queue capacity |
| Flush interval | 250 ms | 10–60,000 ms |
| Default/max query page | 200 | Configured 1–1,000 |
| Pending queries | 16 | 1–128 |
| Query deadline | 2 s | 100–10,000 ms |
| Shutdown flush | 2 s | 100–10,000 ms; missed deadline is logged and exits nonzero |

## 6. Verification performed

### Actual runtime verification

- Clean AppleClang 21 C++23 configure/build passed; CTest passed **12/12**.
- ASan/UBSan configure/build passed; CTest passed **13/13**, including the
  sanitizer coverage audit.
- Portable feature suite passed: C++23 **12/12**, C++20 **12/12**, topology
  validation, finite TCP/shared-memory pipelines, coordinated shutdown,
  telemetry security, authenticated runtime behavior, web build and tests.
- Telemetry suite passed **36/36**. Phase 7 coverage includes strict full-startup
  configuration rejection without creating a database, persistence, stable
  pagination, filters, reduced-policy startup pruning, query-time age
  non-disclosure, idle pruning and maintenance failure, queue overflow, forced
  DB-full degradation, reduced-cap startup rejection, a real writer-lock
  shutdown deadline, schema version, graph ownership, authenticated API, abrupt process
  restart and service-readiness isolation.
- Web tests passed **6/6**, including a mounted history component test for page
  merging, paused refresh, token changes, and response reordering; the Vite
  production build passed.
- `scripts/test-phase7-history.sh` passed against an isolated real container and
  named volume: unauthenticated 401, authenticated UDP write/API retrieval,
  telemetry restart, persistent authenticated reread, and scoped cleanup.
- `scripts/test-phase6-operations.sh` passed after the additions: telemetry and
  Prometheus health, scrape, six rules, query, and Grafana dashboard provisioning.
- Prometheus rule tests passed, including history-disabled suppression and the
  enabled/unavailable alert hold period.
- Both default and history-overlay Compose configurations validated; a no-cache
  telemetry image build succeeded from the immutable Node 22.23.2 Alpine
  digest.
- The verifier's reduced-retention reproduction passed inside that selected
  image runtime: 10 records remained, the expired record was hidden, and 11
  rows were counted as pruned before readiness.
- Telemetry and web npm audits each reported **0 vulnerabilities**.
- The three pre-existing `gx-ipv-side-*`/`gx-ovs-*` containers remained running
  and were not restarted, modified, or removed.

### Code/static inspection

- `git diff --check` passed.
- `jq` validation passed for the JSON schema and 13-panel dashboard.
- A cppcheck-enabled C++20 build completed without findings.
- `clang-format --dry-run --Werror` passed for all modified C++ files using
  installed clang-format 23.1.0.

### Checks not completed with the exact required tool

- `scripts/check-format.sh` exited 2 because the repository requires
  clang-format 18.x and only 23.1.0 is installed. The modified files pass the
  installed formatter, but the exact version gate remains for CI/verifier.
- `scripts/run-static-analysis.sh` exited 2 because `clang-tidy` is unavailable.
  Cppcheck did run and pass; the exact clang-tidy gate remains for CI/verifier.
- `scripts/run-fuzz.sh` configured but could not link either fuzz target because
  this Apple command-line toolchain lacks `libclang_rt.fuzzer_osx.a`; no fuzz
  iteration was executed. Run the fuzz smoke in Linux CI with libFuzzer.
- Privileged native-Linux macvlan/ipvlan/netns/OVS tests were not run on macOS.
  Phase 7 does not change those paths, and the live Phase 7 test requires no
  privilege. Existing simulation containers were deliberately left untouched.

The Vite build retains the pre-existing approximately 1.85 MB chunk-size
warning; it is not a build failure and Phase 7 does not materially change the
existing React Flow bundle architecture.

## 7. Known limitations and verifier focus

- `node:sqlite` remains experimental in Node 22. Verify the image/runtime policy
  is acceptable for Phase 7 and retain the adapter-replacement item for Phase 10.
- SQLite is a local single-writer backend, not replicated or remotely managed.
- `max_database_bytes` limits the main DB; total transient storage includes WAL,
  SHM and filesystem overhead and must be monitored with the exported metric.
- Cursor semantics are stable for backward paging but provide no snapshot
  transaction across multiple HTTP requests; newly inserted higher IDs do not
  disturb an existing backward cursor.
- Online backup, restore orchestration, migration administration, deletion,
  replay and remote backend plugins are intentionally absent.
- Independent reverification should rerun reduced age/count policy startup,
  idle expiry, locked maintenance, mounted pagination state, strict
  empty/unknown YAML startup, reduced-cap, writer-lock deadline, and
  authenticated container cases. Longer filesystem-full/permission,
  query-load, and corruption soaks remain useful, as do the exact clang-format
  18/clang-tidy gates and selected-runtime compatibility.

## 8. Acceptance checklist

- [x] Durable metadata survives process/container restart.
- [x] Storage is optional, default-off and additive.
- [x] Graph processing and service readiness are isolated from storage failure.
- [x] All queues, bytes, pages, batches, queries and deadlines are bounded.
- [x] Retention, schema ownership and migration compatibility are explicit.
- [x] Read endpoints are strict, bounded and observation-authenticated.
- [x] No secrets or application payload bodies are retained.
- [x] Compose persistence and read-only container behavior are compatible.
- [x] Operations metrics, alert, dashboard and console expose truthful state.
- [x] Unit, negative, subprocess, restart and live container tests pass.
- [x] Documentation and ADR match implemented behavior.
- [ ] Exact clang-format 18, clang-tidy, and libFuzzer gates require the missing
  local tools/runtimes or CI; this is explicitly reported rather than claimed
  as passed.

## 9. Recommended next work package

Run `prompt/verifier.md` independently for Phase 7. After acceptance, proceed to
Phase 8 authorized control-plane work. Do not conflate durable history with a
control audit log: Phase 8 must define authorization policy, command identity,
idempotency, acknowledgement, audit semantics and actual runtime control before
adding any history mutation or control-event claims.
