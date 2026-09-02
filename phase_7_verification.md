# Phase 7 independent reverification report

Date: 2026-09-01  
Work package: durable or backend-driven telemetry history  
Candidate handoff: `phase_7_handoff.md`  
Verifier scope: current dirty worktree after the first Phase 7 remediation; implementation files were not modified

## 1. Verdict

**CHANGES REQUIRED**

The five findings from the first Phase 7 verification are remediated. Quoted
booleans/numbers now fail consistently, an existing database above a reduced
cap degrades with an actionable error, a real SQLite writer lock no longer
defeats the shutdown deadline, both image stages are immutable, and the live
container test now proves unauthenticated denial and authenticated persistence.

One remaining P2 configuration-parity defect prevents acceptance: the Node
telemetry runtime silently treats explicit empty YAML `backend` and
`database_file` values as absent and ignores unknown history keys, while the
JSON schema and C++ validator reject them. A full-service probe with history
enabled, both strings empty, and a misspelled retention key started a ready
SQLite backend and created the default database. This can silently select an
unintended storage path and default retention policy.

No P0 or P1 issue was found. One non-blocking P3 console pagination issue is
also recorded.

## 2. Executive summary

Phase 7's core architecture remains sound. SQLite is owned by a dedicated
worker behind bounded queues. History is optional and default-disabled; graph
nodes do not become storage clients; service readiness remains independent from
backend health; writes are transactional and durable; count/age/main-file
limits, schema ownership, stable cursors, read-only authenticated APIs, metrics,
alerts, dashboard panels, a console view, and an opt-in Compose volume are
implemented.

Independent runtime verification passed fresh native and sanitizer builds,
telemetry and web tests, portable C++20/C++23 behavior, a no-cache pinned image
build, live authenticated volume persistence, abrupt process recovery,
adversarial API responses, Prometheus/Grafana provisioning, npm audits, and a
cppcheck-enabled build. The writer-lock service probe exited 1 with a truthful
diagnostic in 104.1 ms under a 100 ms history budget. A normal 2,633,728-byte
database reopened under the 1 MiB setting as degraded, not ready.

The remaining P2 occurs because `historyConfig()` uses JavaScript `||` for YAML
strings and has no unknown-key check. This contradicts the handoff's claim of
identical configuration semantics and the Phase 7 requirement for strict,
authoritative configuration. Phase 8 should not begin until this is fixed and
reverified.

## 3. Acceptance-criteria matrix

| Acceptance criterion | Status | Independent evidence |
|---|---|---|
| Validated operational telemetry persists across collector restart | Pass | Authenticated subprocess test retained a committed record across `SIGKILL`; isolated Compose test retained sequence 7007 across container restart. |
| Storage cannot block graph processing or service readiness | Pass | Dedicated worker and bounded offer confirmed; schema/DB-full tests leave `/api/ready` healthy while history degrades. |
| History is optional, additive, and default-disabled | Pass | Checked-in `graphx.yaml` is disabled; default Compose renders without the writable history volume; overlay is opt-in. |
| Configuration semantics agree across schema, C++ loader, Node runtime, and environment | Fail | Empty YAML backend/path and unknown keys are rejected by C++/schema but accepted by Node; Finding 1. Quoted scalar and override cases are fixed. |
| Queue count/bytes, record bytes, batches, queries, retention and database size are bounded | Pass | Unit/adversarial tests cover queue/full behavior, count pruning, query bounds and reduced-cap startup; oversized DB degrades. |
| Shutdown obeys the configured elapsed deadline and reports failure distinctly | Pass | Store lock probe returned in 109.9 ms; full service exited 1 in 104.1 ms and logged `GraphX durable history shutdown failed`. |
| Schema versions and graph ownership fail closed | Pass | Newer schema and other-graph tests pass and history rejects reads. |
| Query API is strict, bounded, stable and read-only | Pass | Live responses: no token 401, valid status 200, oversized limit/unknown key/unknown node 400, POST 405. |
| Observation authorization does not expand control authority | Pass | History has GET-only routes under the observation token; control credentials and routes remain separate. |
| Stored data excludes payload bodies, credentials and capture contents | Pass | Record shaping is a bounded operational allowlist; no application envelope body, token, secret, OTLP credential, control credential or capture data field is stored. |
| Backend health, metrics, alert and dashboard reflect actual state | Pass | Status/metrics failure transitions, enabled-aware rule tests, live six-rule Prometheus load and Grafana provisioning pass. |
| Compose persistence preserves least privilege | Pass | Live container: `USER node`, read-only root, all capabilities dropped, no-new-privileges, PID limit 128, writable 0750 history directory only through the named volume. |
| Documentation accurately describes limits and runtime behavior | Partial | Database cap, shutdown, pinning and authentication claims now match behavior; strict cross-runtime configuration claim is contradicted by Finding 1. |
| Tests cover realistic persistence, overload, incompatibility and restart failures | Partial | Broad coverage passes, including all prior reproductions; empty-string/unknown-key parity and persistent console pagination are missing. |
| Wire/config version compatibility and prior phases remain healthy | Pass | Configuration remains version 1; protocol, mixed-version, lifecycle, C++20/C++23, security, TCP/shared-memory and operations regressions pass. |
| No later-phase implementation was introduced | Pass | No history mutation/control/replay/delete API and no Phase 9 PCAPNG indexing/dissector behavior were added. |

## 4. Findings

### P2 — Telemetry still accepts YAML history values rejected by the authoritative validator

- **Component/location:** `apps/telemetry/history.mjs:42-54`;
  telemetry loading at `apps/telemetry/server.mjs:23,33`;
  strict schema at `config/schema/graphx.schema.json:362-379`; C++ strict keys
  and string validation in `src/config.cpp` history parsing.
- **Reproduction/evidence:** `historyConfig({enabled:false, backend:""}, {})`
  returned backend `sqlite`; `historyConfig({enabled:false,
  database_file:""}, {}, "/tmp")` returned
  `/tmp/.graphx/history.sqlite`; and a config containing
  `unknown_policy: true` was accepted. The C++ CLI rejected the empty YAML file
  with exit 2 and diagnostics `history.backend: must not be empty` and
  `history.database_file: must not be empty`. In a full telemetry-service
  reproduction, `enabled: true`, empty backend/path, and
  `typo_retention_seconds: 60` started history as `ready` and created
  `.graphx/history.sqlite` beside the temporary configuration.
- **Expected:** explicit invalid YAML values and unknown keys must fail telemetry
  startup exactly as they fail the schema/C++ boundary. Only a missing YAML
  property or a deliberately empty environment override should retain a
  documented fallback.
- **Actual:** `env.GRAPHX_HISTORY_BACKEND || config.backend || "sqlite"` and the
  equivalent database-file expression conflate an explicit empty YAML string
  with absence. `historyConfig()` never checks the YAML object's key set, so
  misspelled limits silently fall back to defaults.
- **Impact:** an operator typo can enable SQLite at an unintended default path,
  retain data for the default duration instead of the requested policy, consume
  unexpected disk, and make the service report ready even though authoritative
  validation would reject the same file. This is a storage-policy and
  configuration-compatibility violation.
- **Remediation:** validate the YAML object against an explicit allowed-key set;
  validate `config.backend` and `config.database_file` before applying
  defaults; use nullish/explicit-presence logic for YAML rather than truthiness;
  and keep the separately documented empty-environment behavior. Prefer one
  shared fixture set exercised through schema/C++, `historyConfig()`, and full
  service startup.
- **Missing regression test:** valid missing properties; empty YAML backend and
  database path; unknown/misspelled key; empty environment override; valid
  environment override; and full startup must produce matching outcomes across
  all configuration boundaries.

### P3 — Automatic console refresh discards paged history

- **Component/location:** `web/src/components/HistoryPanel.jsx:13-33,55`.
- **Evidence:** clicking **Load older records** calls `load(cursor, true)` and
  appends a page. The unconditional five-second interval calls `load()` with
  `append=false`, which replaces the entire record list with the newest page.
  The older page therefore disappears at the next poll. No HistoryPanel test
  exercises pagination or refresh ordering.
- **Expected:** paging backward should remain visible while live refresh either
  prepends/deduplicates new records or pauses until the user returns to the
  newest page.
- **Actual:** the periodic newest-page refresh resets pagination state every five
  seconds and can race an in-flight append request.
- **Impact:** no stored data is lost, but operators cannot reliably inspect more
  than the newest 100 records in the console.
- **Remediation:** preserve the cursor/page chain and merge records by ID, or
  suspend automatic refresh while older pages are displayed; ignore stale
  responses with an abort/generation mechanism.
- **Missing regression test:** component test with fake timers covering load
  older, refresh, response reordering and ID deduplication.

## 5. Tests and checks run

All commands ran from `/Users/rklinkhammer/workspace/graphx-docker` unless noted.

| Check | Exact result |
|---|---|
| Fresh C++23 Debug configure/build in `build/phase7-reverifier-clean` | Passed with AppleClang 21.0.0 and OpenSSL 3.6.3. |
| Complete fresh CTest | **12/12 passed** in 4.96 s. |
| Fresh ASan/UBSan build in `build/phase7-reverifier-sanitizers` | **13/13 passed** in 9.60 s, including sanitizer coverage audit. |
| Fresh telemetry install/test/audit | **32/32 passed** in 0.69 s; 0 vulnerabilities. |
| Fresh web install/test/build/audit | **3/3 passed**; Vite production build passed; 0 vulnerabilities. Existing approximately 1.85 MB chunk warning remains. |
| Portable feature suite | Passed C++23 **12/12**, C++20 **12/12**, every topology validation, finite TCP/shared-memory pipelines, coordinated shutdown, telemetry/web, authenticated runtime control and HTTPS/API checks. |
| Default and history-overlay Compose rendering | Passed with a bounded observation-token value. |
| No-cache affected telemetry image build | Passed from immutable digest `sha256:c610fcdf...aa32`; resulting runtime is Node 22.23.2 and image user is `node`. |
| Live Phase 7 Compose test | Passed unauthenticated 401, authenticated UDP write/query, container restart, authenticated persistent reread and scoped cleanup. |
| Live API adversarial probe | Returned 401/200/400/400/400/405 for absent auth, valid status, oversized limit, unknown query key, unknown node and POST respectively. |
| Container hardening inspection | Passed read-only root, `USER node`, `cap_drop: ALL`, no-new-privileges, PID limit 128, 0750 writable history volume and non-writable `/app`. |
| Abrupt process recovery | Passed in telemetry test: committed sequence 77 survived `SIGKILL` and restart. |
| Reduced-cap independent probe | A 2,633,728-byte normal DB reopened degraded under the 1 MiB setting with an effective 983,040-byte main-file diagnostic. |
| Locked shutdown independent probe | Store close returned failure in **109.9 ms**; full service exited **1** in **104.1 ms** with a shutdown-failed diagnostic. |
| Quoted-scalar parity probe | C++ exited 2 and Node rejected quoted false/number; valid and empty environment precedence produced the documented values. |
| Empty/unknown configuration probe | Failed acceptance: Node accepted empty backend/path and unknown keys; full service started ready and created the default DB while C++ rejected the file. |
| Operations stack regression | Passed telemetry/Prometheus health, scrape/query, six rules and Grafana dashboard provisioning. |
| Prometheus alert tests | Passed through the operations script, including disabled suppression and enabled/unavailable hold period. |
| Fresh cppcheck-enabled C++20 build | Passed with cppcheck 2.21.0 and no findings. |
| Repository format script | Unverified: exited 2 because clang-format 18.x is required and installed version is 23.1.0. |
| Available clang-format on changed C++ | Passed `--dry-run --Werror` with clang-format 23.1.0. |
| clang-tidy/static script | Partially unverified: exited 2 because clang-tidy is absent; cppcheck passed separately. |
| Fuzz smoke | Unverified: targets could not link because `libclang_rt.fuzzer_osx.a` is absent; no fuzz iteration ran. |
| JSON/shell/diff hygiene | Schema/dashboard JSON parsed, changed shell scripts passed `bash -n`, and `git diff --check` passed. |

Verifier-created Compose projects, networks and volumes were removed. The
pre-existing `gx-ipv-side-sink-1`, `gx-ipv-side-transform-1`, and
`gx-ovs-ovs-router-1` containers remained running and were not changed.

## 6. Unverified areas and why

- The exact clang-format 18 gate could not run because only 23.1.0 is installed.
- clang-tidy is not installed; cppcheck did run in a fresh instrumented build.
- Fuzz targets could not link because the Apple command-line toolchain lacks
  `libclang_rt.fuzzer_osx.a`.
- Privileged native-Linux macvlan, ipvlan, namespaces and Open vSwitch tests
  were not rerun on macOS. Phase 7 does not modify those paths.
- No long-duration disk-growth, concurrency, filesystem-corruption, power-loss
  or permission-transition soak was performed. Deterministic capacity,
  SQLite-full, lock, graceful restart and abrupt-death paths were exercised.
- No remote history backend was tested because SQLite is the only implemented
  backend and satisfies the “durable or backend-driven” scope.

These restrictions do not cause the verdict; Finding 1 was reproduced with the
available local runtime.

## 7. Compatibility and security assessment

### Compatibility

The GraphX wire envelope, framing, transport interfaces, configuration version
and graph lifecycle semantics remain unchanged. History is an additive
telemetry-service concern and does not couple the GraphX graph model to Docker,
SQLite or the GUI. Protocol boundaries, mixed-version behavior and the full
C++20/C++23 portable suite pass.

The exception is configuration parity. The schema and C++ loader are strict,
but the Node service is a second direct YAML consumer and currently accepts a
subset of invalid files. This must be corrected before the configuration can be
called authoritative across deployment paths.

### Security and privacy

History reads reuse observation authorization and do not grant control
authority. Live tests confirm bearer denial/success, strict methods and bounded
queries. Stored records are bounded operational metadata and exclude envelope
payload bodies and credential fields. Schema identity and graph ownership fail
closed. The opt-in volume is isolated from graph nodes and the service retains a
read-only root, non-root user, dropped capabilities and no-new-privileges.

Finding 1 has a security/retention-policy consequence: malformed but accepted
configuration can create a database at a default location or use a longer
default retention period. No authentication bypass, injection, path traversal,
credential leakage or Phase 8 authority expansion was found.

## 8. Required remediation before acceptance

1. Make `historyConfig()` reject unknown YAML keys and explicit empty
   `backend`/`database_file` values while retaining the documented empty
   environment-override behavior.
2. Add shared cross-boundary fixtures and a full-startup regression for missing,
   empty, unknown, valid and overridden history values.
3. Rerun telemetry, clean CTest/sanitizers, portable, no-cache image, live
   authenticated persistence, operations and configuration adversarial checks.

The console pagination P3 is non-blocking for Phase 7 acceptance but should be
scheduled with a focused component test.

## 9. Readiness for the next work package

The project is **not ready for Phase 8**. Phase 7 requires the configuration
parity remediation and independent reverification first.

After Phase 7 is accepted, Phase 8 authorized control-plane work must preserve:

- separation between control authority and observation/history access;
- graph progress and service readiness independence from durable storage;
- explicit bounds for history queues, writes, queries, retention and shutdown;
- versioned schema and graph ownership that fail closed;
- no secret or application payload body in durable history; and
- no mutation/replay of history disguised as control or audit behavior.
