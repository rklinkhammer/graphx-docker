# Phase 7 independent reverification report

Date: 2026-09-02

Work package: durable or backend-driven telemetry history

Candidate handoff: `phase_7_handoff.md`

Verifier scope: current dirty worktree after the third Phase 7 remediation;
implementation files were not modified by this verification pass

## 1. Verdict

**ACCEPTED**

All Phase 7 acceptance criteria are met. The previous P2 retention defect and
P3 console-test gap are closed by implementation evidence and fresh behavioral
verification. No P0-P3 finding remains from this pass.

Phase 7 is ready to become the foundation for Phase 8, provided Phase 8
preserves the compatibility, isolation, authorization, resource-bound, and
data-minimization invariants listed in section 9.

## 2. Executive summary

GraphX now provides optional, default-disabled, SQLite-backed telemetry and SLO
history without coupling graph execution to Docker, the web console, or durable
storage. The telemetry process offers bounded metadata records to an isolated
worker thread that exclusively owns the synchronous SQLite connection. Storage
startup, writes, queries, maintenance, degradation, and shutdown have explicit
states and bounds. Storage failure does not make the graph or telemetry service
unready.

The remediation now enforces age and record-count retention before an existing
database reports ready, before and after batches, during bounded idle
maintenance, and during graceful close. Queries independently apply the age
cutoff, preventing disclosure between maintenance cycles. Maintenance results
update prune and disk metrics; maintenance failure degrades history and clears
bounded pending work without blocking the service thread. Fresh regressions
proved reduced-policy cleanup on reopen, query-time non-disclosure, idle
physical pruning, and degradation under a real SQLite writer lock.

The web console's history view now has a mounted JSDOM/React regression in
addition to its pure state-helper tests. It exercises real effect/timer wiring,
load-older and return-to-newest controls, paused polling, cursor propagation,
credential changes, response reordering, and unmount with an outstanding
request.

Fresh verification passed native C++23 and C++20 builds, the full CTest and
sanitizer suites, 36 telemetry tests, six web tests and the production build,
dependency audits, the portable feature suite, Compose rendering, a no-cache
telemetry image build, authenticated durable history across a real container
restart, strict API responses, container hardening, and the Phase 6
Prometheus/Grafana regression. The exact pinned formatter, clang-tidy, and fuzz
jobs remain unavailable on this Mac for the environmental reasons in section
6; the available clang-format and Cppcheck checks passed.

## 3. Acceptance-criteria matrix

| Acceptance criterion | Status | Independent evidence |
|---|---|---|
| Validated operational telemetry persists across collector restart | Pass | Abrupt-process restart regression passed; isolated Compose test retained sequence 7007 across telemetry-container restart. |
| Storage cannot block graph processing or service readiness | Pass | SQLite is confined to a worker behind bounded queues; lock/failure tests degrade history independently, and `/api/ready` remains separate. |
| History is optional, additive, and default-disabled | Pass | `graphx.yaml` defaults history off; the writable database volume is introduced only by `compose.history.yaml`. |
| Configuration agrees across schema, C++ loader, Node runtime, and environment | Pass | C++/Node regressions reject wrong scalar types, empty or unknown YAML, invalid ranges, and invalid override masking; empty environment values retain valid YAML. |
| Count, byte, batch, query, record, database, and timeout bounds are explicit | Pass | Configuration and tests cover queue items/bytes, record bytes, batches, pending queries, query limits/deadlines, database pages, and shutdown. |
| Existing databases obey reduced retention before readiness | Pass | Fresh telemetry regression reopened 21 rows under a 60-second/10-record policy and observed ready with 10 rows, no expired row, and at least 11 pruned. |
| Idle age/count retention remains enforced without writes | Pass | Query-time cutoff hid an injected expired row immediately; bounded idle maintenance physically removed it and advanced prune metrics. |
| Maintenance failure is truthful and non-blocking | Pass | A real `BEGIN IMMEDIATE` writer lock caused idle maintenance to transition history to `degraded` without blocking the service thread. |
| Shutdown obeys one elapsed deadline and reports failure distinctly | Pass | SQLite writer-lock regression bounded failed close near the configured 100 ms minimum; portable coordinated-shutdown checks passed. |
| Schema versions and graph ownership fail closed | Pass | Newer-schema and other-graph tests passed and made history unavailable without changing service readiness. |
| Query API is strict, bounded, stable, authenticated, and read-only | Pass | Live results were 401/200/400/400/400/405 for absent auth, valid status, excessive limit, unknown key, unknown node, and POST. Pagination/cursor tests passed. |
| Observation authorization does not expand control authority | Pass | History endpoints are GET-only and use the observation bearer; control credentials and routes remain separate. |
| Stored data excludes payload bodies, credentials, and capture contents | Pass | Record construction is a bounded operational allowlist; envelope payload bodies, tokens, secrets, credentials, and capture bodies are not persisted. |
| Backend health, metrics, alert, and dashboard reflect runtime state | Pass | Failure-state tests, history metrics, enabled-aware alerting, six live Prometheus rules, scrape/query checks, and Grafana provisioning passed. |
| Compose persistence preserves least privilege | Pass | Live container used `node`, a read-only root, `cap_drop: ALL`, no-new-privileges, PID limit 128, non-writable `/app`, and a writable node-owned 0750 history directory. |
| Console supports stable older-page exploration and safe refresh | Pass | Six web tests passed; mounted component coverage proved pause/resume, cursor use, timer behavior, reordered response suppression, token change, and safe unmount. |
| Documentation describes actual behavior and limits | Pass | `README.md`, `docs/history.md`, ADR 0008, test procedure, and handoff agree with inspected and exercised retention, API, deployment, and console behavior. |
| Wire/config compatibility and prior phases remain healthy | Pass | Configuration remains version 1; C++20/C++23, protocol, lifecycle, security, TCP/shared-memory, HTTPS, and Phase 6 operations regressions passed. |
| No later-phase behavior was introduced | Pass | No history mutation/replay/delete control, PCAPNG/Wireshark Phase 9 expansion, or release-engineering Phase 10 behavior was added. |

## 4. Findings

No P0, P1, P2, or P3 finding remains.

### Closure of findings from the preceding report

| Prior finding | Resolution evidence | Status |
|---|---|---|
| P2: existing and idle databases violated age/count retention | `apps/telemetry/history-worker.mjs` runs maintenance before ready and on a bounded interval, applies an age predicate to queries, reports maintenance statistics/errors, and clears the timer on close. Three focused telemetry regressions passed, including a real SQLite lock. | Closed |
| P3: console paging lacked a mounted timing regression | `web/src/history.test.mjs` mounts `HistoryPanel` through Vite SSR into JSDOM and exercises the timer, buttons, cursor, paused refresh, return to newest, request ordering, token change, and unmount. | Closed |

## 5. Tests and checks run

All commands ran from `/Users/rklinkhammer/workspace/graphx-docker`.

| Check / command | Exact result |
|---|---|
| Fresh C++23 Debug configure/build in `build/phase7-verifier4-clean`; complete `ctest --test-dir ... --output-on-failure` | Passed with AppleClang 21.0.0 and OpenSSL 3.6.3; **12/12 passed** in 5.87 s. |
| Fresh ASan/UBSan configure/build/test in `build/phase7-verifier4-sanitizers` | **13/13 passed** in 10.96 s, including sanitizer-coverage audit. |
| `npm ci`, `npm test`, and `npm audit` in `apps/telemetry` | **36/36 passed** in 1.25 s; **0 vulnerabilities**. |
| `npm ci`, `npm test`, `npm run build`, and `npm audit` in `web` | **6/6 passed** in 1.26 s; mounted history test passed in 182.9 ms; Vite production build passed; **0 vulnerabilities**. Existing approximately 1.85 MB chunk warning remains. |
| `GRAPHX_BUILD_DIR=... GRAPHX_CXX20_BUILD_DIR=... scripts/test-features.sh portable` | Passed fresh C++23 **12/12** and C++20 **12/12**, topology validation, TCP/shared-memory pipelines, coordinated shutdown, telemetry/web suites, authenticated controls, HTTPS/API behavior, and secure bind defaults. |
| `docker compose -f compose.yaml config --quiet` and default plus `compose.history.yaml` | Both configurations rendered successfully with a valid bounded observation token. |
| `docker build --no-cache -f docker/telemetry.Dockerfile -t graphx-telemetry:phase7-verifier4 .` | Passed from the immutable Node 22 Alpine digest; image SHA-256 `95d8bee48c598e16b23fbec0e329c5a76fcba1f6a8b5e4dbb6b7db8919222ff7`. |
| `GRAPHX_PHASE7_TEST_PROJECT=graphx-phase7-verifier4-live scripts/test-phase7-history.sh` | Passed authentication denial, UDP event write, query, real telemetry-container restart, persistent reread, named-volume use, and scoped cleanup. |
| Targeted retention regressions in `apps/telemetry/history.test.mjs` | Reopen under reduced policy returned exactly 10 records, hid the 120-second-old record, and pruned at least 11; idle query hiding/prune and locked-maintenance degradation passed. |
| Live adversarial history API probe | Returned **401/200/400/400/400/405** for no auth, valid status, excessive limit, unknown query key, unknown topology node, and POST. |
| Live container-hardening inspection | Reported `node`, `readonly=true`, `caps=["ALL"]`, `no-new-privileges:true`, and `pids=128`; `/app` was not writable and history was `750:node:node`. |
| `GRAPHX_PHASE6_TEST_PROJECT=graphx-phase6-verifier4 scripts/test-phase6-operations.sh` | Passed telemetry and Prometheus health, target scrape, readiness query, all six alert rules, and Grafana dashboard provisioning. |
| Fresh Cppcheck-enabled C++20 build in `build/phase7-verifier4-cppcheck` | Passed using Cppcheck 2.21.0 with no findings. |
| `clang-format --dry-run --Werror tests/test_config.cpp` | Passed with installed clang-format 23.1.0. |
| `scripts/check-format.sh` | Not executed to completion: exited 2 because the repository requires clang-format 18.x and this host has 23.1.0. |
| `scripts/run-static-analysis.sh` | Partially unverified: exited 2 because clang-tidy is not installed; the independent Cppcheck build passed. |
| `scripts/run-fuzz.sh` | No fuzz iteration ran: targets could not link because Apple Command Line Tools lacks `libclang_rt.fuzzer_osx.a`. |
| `bash -n` over repository shell scripts and `jq empty` over schema/dashboard JSON | Passed after using null-glob-safe file enumeration. |
| `git diff --check` | Passed. The dirty worktree contains the implementation candidate and pre-existing user prompt edits; no implementation file was changed by the verifier. |

Verifier-created Compose containers, networks, and volumes were removed by the
test cleanup paths. The pre-existing `gx-ipv-side-sink-1`,
`gx-ipv-side-transform-1`, and `gx-ovs-ovs-router-1` containers remained running
and were not modified.

## 6. Unverified areas and why

- The exact clang-format 18 gate could not run because only clang-format 23.1.0
  is installed. The changed C++ file passes the available formatter.
- clang-tidy is unavailable. Cppcheck did run across a fresh instrumented C++20
  build and reported no finding.
- Fuzz targets could not link because this Apple command-line toolchain lacks
  `libclang_rt.fuzzer_osx.a`; no fuzz iteration was claimed.
- Privileged native-Linux macvlan, ipvlan, network-namespace, and Open vSwitch
  scenarios were not rerun on macOS. Phase 7 does not modify those paths; the
  three existing mixed-network containers were deliberately left untouched.
- No long-duration disk-growth, corruption, power-loss, backup/restore,
  permission-transition, or multi-process writer soak was performed.
  Deterministic database-full, writer-lock, queue pressure, restart, abrupt
  death, schema mismatch, graph mismatch, and retention paths were exercised.
- SQLite is the only implemented Phase 7 backend, so no remote history backend
  was tested. The backend seam is documented; a remote backend is not an
  acceptance requirement for this phase.

These restrictions do not block acceptance because the affected Phase 7 paths
were verified through clean functional, sanitizer, Cppcheck, container, and
failure-path coverage. They must not be represented as having passed the exact
unavailable gates.

## 7. Compatibility and security assessment

### Compatibility

The GraphX envelope, framing, transport interfaces, configuration version,
graph lifecycle, and Docker-independent graph model remain unchanged. History
is an additive telemetry-service concern and is disabled by default. The
dedicated worker and SQLite schema do not enter the graph execution dependency
direction. Full portable C++20/C++23, protocol, transport, lifecycle, HTTPS,
configuration, and operations regressions passed.

Database schema version and graph ownership are explicit and fail closed.
Reduced current policies are applied before readiness, so restarting with a
smaller age/count policy no longer creates a compatibility gap between accepted
configuration and observable behavior.

### Security and privacy

History reads reuse observation authorization but never grant control
authority. Live probes confirmed bearer denial, bounded and topology-aware
query validation, GET-only behavior, and least-privilege container settings.
Stored rows contain bounded operational metadata and exclude application
payload bodies, authentication material, capture bodies, and arbitrary input
fields. Database size, record size, queue count/bytes, query concurrency,
timeouts, retention, and shutdown are bounded.

Expired rows are filtered at query time and physically pruned at startup,
during writes, on bounded idle maintenance, and on graceful close. A
maintenance lock/failure is reported as degraded rather than falsely healthy.
No authentication bypass, injection, traversal, credential leakage, control
authority expansion, secret logging, or graph-progress coupling was found.

## 8. Required remediation before acceptance

None. Phase 7 is accepted.

Non-blocking later work should retain the existing Vite bundle-size warning in
the performance backlog and account for Node's `node:sqlite` maturity/support
status during Phase 10 release qualification. The exact CI toolchain should
continue to run pinned clang-format 18, clang-tidy, and libFuzzer on supported
runners; this local Mac result does not replace those gates.

## 9. Readiness for the next work package

The project is **ready for Phase 8: authorized control plane and real runtime
controls**.

Phase 8 must preserve these invariants:

- control authority remains distinct from observation/history authorization;
- GraphX semantics and graph progress remain independent of Docker placement,
  the GUI, telemetry vendors, and durable storage;
- history remains optional, default-disabled, metadata-only, and read-only at
  its observation API;
- control commands cannot mutate, delete, replay, or relabel durable history;
- all control inputs, queues, payloads, retries, deadlines, and shutdown paths
  are explicitly bounded and report unambiguous outcomes;
- configuration and wire compatibility remain versioned and fail closed;
- schema and graph ownership checks continue to reject incompatible stores;
- credentials and application payload bodies never enter durable history,
  metrics, diagnostic logs, or control responses; and
- Phase 8 adds real authorized runtime behavior, not UI-only or simulated
  controls that appear operational.
