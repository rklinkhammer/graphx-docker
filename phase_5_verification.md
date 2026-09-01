# Phase 5 Independent Verification Report

**Verdict: CHANGES REQUIRED**

Date: 2026-09-01  
Repository: `/Users/rklinkhammer/workspace/graphx-docker`  
Work package: Phase 5 — authentication, TLS, API validation, and container hardening  
Verifier basis: `prompt/verifier.md`, `prompt/implement.md`, repository inspection, clean builds, tests, and independent adversarial checks

## 1. Executive summary

Phase 5 contains substantial, generally well-factored security work. Optional TLS 1.3/mTLS is additive to TCP edges; telemetry datagrams have HMAC, freshness, and replay checks; observation and control credentials are separated; unsafe remote plaintext requires an explicit opt-in; the portable containers drop capabilities and run non-root; configuration version 1 and the existing frame/envelope formats are preserved. Clean C++20 and C++23 builds, all 12 native tests, all 13 sanitizer checks, the complete portable feature suite, telemetry security tests, dependency audits, Compose validation, both affected image builds, and cppcheck passed independently.

The work is not acceptable yet because four production-significant defects remain:

1. A single unauthenticated malformed HTTP request target terminates the telemetry process with an uncaught `ERR_INVALID_URL` exception.
2. The two container images initialize the shared capture volume with incompatible UID/mode combinations, so native nodes cannot write captures when telemetry initializes the volume first.
3. The HTTP rate-limit map is documented and handed off as bounded at 2,048 entries, but the implementation does not enforce that bound while entries are live.
4. TLS application-data I/O does not change poll direction after OpenSSL returns `SSL_ERROR_WANT_WRITE` from a read or `SSL_ERROR_WANT_READ` from a write, allowing avoidable stalls or timeout failures.

Frontend authentication/download changes also have no browser test target. The exact clang-format 18 and clang-tidy 18 gates, remote CI, Linux-specific TLS behavior, and privileged Linux networking laboratories could not be run on this macOS host and are explicitly not claimed as verified.

The project must not proceed to Phase 6 until the P1/P2 findings are corrected and independently reverified.

## 2. Scope and acceptance-criteria matrix

| Requirement / acceptance criterion | Implementation evidence | Validation evidence | Status |
|---|---|---|---|
| Optional per-edge TLS 1.3 without wire-format changes | `include/graphx/config.hpp`; `include/graphx/tcp_transport.hpp`; `src/config.cpp`; `src/transport_factory.cpp`; `src/tcp_transport.cpp` | Clean C++20/C++23 builds and TLS smoke pass; configuration remains version 1; existing protocol/mixed-version tests pass | PASS |
| Certificate-chain and endpoint-name verification by default | `src/tcp_transport.cpp:280-313` | Peer-name mismatch is rejected by `tests/tls_smoke.cpp`; CA verification flags inspected. Separate untrusted-CA negative was not exercised | PARTIAL |
| Mutual TLS and bounded handshake cancellation | `src/tcp_transport.cpp:295-325`; `tests/tls_smoke.cpp` | Ephemeral mTLS round trip and stalled-handshake cancellation pass | PASS |
| Correct encrypted application-data I/O, deadlines, and reconnect semantics | `src/tcp_transport.cpp:182-253` | Positive TLS round trip passes; static state-machine inspection found wrong readiness selection after cross-direction `SSL_ERROR_WANT_*` outcomes | FAIL |
| Secrets remain outside `graphx.yaml`; inline and `_FILE` forms are bounded and unambiguous | `apps/telemetry/security.mjs:4-17`; `apps/common.hpp`; `docs/security.md` | Unit tests cover weak and ambiguous secret inputs; code inspection confirms 32–4,096 byte bounds and one trailing newline removal | PASS |
| Authenticate UDP telemetry, commands, and acknowledgements before state/control endpoint mutation | `apps/telemetry/security.mjs`; `apps/telemetry/server.mjs:433-505`; `src/observability.cpp` | Tamper/replay/staleness tests pass; portable real-runtime authenticated pause/resume passes | PASS |
| Validate telemetry identities, event types, numeric bounds, capture names, and datagram size | `apps/telemetry/security.mjs:5,66-89`; `apps/telemetry/server.mjs:437-505` | Security unit tests and portable contract checks pass | PASS |
| Separate observation and control credentials; authenticate reset/pause/resume | `apps/telemetry/server.mjs:330-383`; `web/src/App.jsx:83-95` | Portable HTTPS test proves observation 401/200, methods, and authenticated real pause/resume; code inspection confirms reset uses control path | PASS |
| HTTPS TLS 1.3 and optional client certificates | `apps/telemetry/server.mjs:408-413`; `docs/security.md` | Portable ephemeral HTTPS check passes. HTTP mTLS was inspected but not independently exercised | PARTIAL |
| Treat request targets and WebSocket upgrade URLs as untrusted without process failure | `apps/telemetry/server.mjs:322-325,419-425` | Independent raw request `GET http://[` causes uncaught `TypeError [ERR_INVALID_URL]` and process exit | FAIL |
| Explicit HTTP methods, bodies, origins, headers, target length, timeouts, and per-client rates | `apps/telemetry/server.mjs:235-253,322-425` | Portable API checks pass for normal negative cases; malformed target crashes; rate-state cap is not enforced | FAIL |
| Bounded rate-limit state (claimed maximum 2,048 entries) | `apps/telemetry/server.mjs:243-253`; `phase_5_handoff.md:85`; `docs/security.md` | Static proof: after size exceeds 2,048, only expired entries are removed; all live entries remain | FAIL |
| Browser observation token protects initial API, WebSocket, and capture download; control token remains memory-only | `web/src/useTelemetry.js`; `web/src/App.jsx`; `web/src/components/EdgeInspector.jsx` | Production bundle builds. No frontend test/lint target exists; `npm test` fails with “Missing script: test” | PARTIAL |
| Plain HTTP defaults to loopback; non-loopback plaintext requires explicit acknowledgement | `apps/telemetry/server.mjs:16-49`; `docs/security.md` | Portable suite proves unsafe remote plaintext startup fails; Compose's private-network override is explicit and host publication is loopback-only | PASS |
| Portable containers run non-root with read-only roots, no capabilities, no privilege escalation, PID limit, init, and tmpfs | `Dockerfile`; `docker/telemetry.Dockerfile`; `compose.yaml` | Compose model validates; images build; isolated Compose/runtime inspection confirms declared hardening | PASS |
| Shared capture volume remains functional under hardened identities | `Dockerfile:14,21`; `docker/telemetry.Dockerfile:10,17`; `compose.yaml:15,35,56,82` | Deterministic volume-initialization test: telemetry creates UID 1000/GID 1000 mode 0750; native UID 65532 gets `Permission denied` | FAIL |
| C++20/C++23, sanitizer, portable, web, dependency, Compose model/image checks | CMake/CI/scripts and lockfiles | All listed checks pass except exact format/tidy gates unavailable and frontend has no tests | PARTIAL |
| Preserve configuration v1, framing/envelope bytes, four transports, and transport-neutral architecture | Config/schema/transport factory and protocol tests | All configuration, protocol-boundary, mixed-version, and transport lifecycle tests pass; no Docker dependency in core semantics | PASS |
| Do not prematurely implement Phase 6–8 systems | Repository and handoff inspection | No OpenTelemetry exporter/SLO/dashboard, durable history, or multi-user authorization/audit subsystem introduced | PASS |
| Documentation describes actual limits and deployment behavior | `README.md`; `docs/security.md`; `docs/observability.md`; handoff | Most boundary documentation is clear, but the 2,048 rate-state claim and capture-volume operability claim do not match behavior; verifier prompt names the Phase 4 handoff | FAIL |

## 3. Findings

### P1 — Malformed unauthenticated request target terminates telemetry

**Location:** `apps/telemetry/server.mjs:322-325`; analogous unguarded parse at `apps/telemetry/server.mjs:419-420`.

**Evidence / reproduction:** Start the telemetry server on loopback and send this raw request:

```text
GET http://[ HTTP/1.1
Host: 127.0.0.1:18083
Connection: close
```

Node accepts the request line and invokes the handler. `new URL(request.url, ...)` throws:

```text
TypeError: Invalid URL
code: 'ERR_INVALID_URL'
input: 'http://['
base: 'http://graphx.invalid'
Node.js v26.8.1
```

The process exits immediately; subsequent connection attempts receive `ECONNREFUSED`. The same unchecked construction exists in the WebSocket upgrade callback.

**Expected:** Every syntactically invalid target is rejected with a bounded 400 response (or the socket is closed for upgrade) while the service remains healthy.

**Actual:** One unauthenticated request causes service termination.

**Impact:** Remote denial of service whenever the telemetry listener is reachable, including secured remote deployments. Authentication does not mitigate it because parsing occurs before authorization.

**Required remediation:** Put URL parsing in a small total function with `try/catch`; return 400 for HTTP and destroy only the upgrade socket for WebSocket. Install server-level `clientError` handling for parser errors and ensure no request/upgrade callback exception can escape. Preserve the 2,048-character precheck.

**Missing regression:** Raw-socket HTTP and upgrade tests covering invalid absolute-form targets, malformed percent escapes, overlong targets, invalid methods/headers, and a post-input health request proving process survival.

### P2 — Shared capture volume ownership is initialization-order dependent and can disable capture

**Location:** `Dockerfile:14,21`; `docker/telemetry.Dockerfile:10,17`; `compose.yaml:15,35,56,82`.

**Evidence / reproduction:** A fresh named volume was first mounted into `graphx-telemetry:latest`, then into `graphx-demo:latest`:

```text
after_telemetry uid=1000 gid=1000 mode=750
telemetry_write_ok
native_view uid=1000 gid=1000 mode=750
touch: cannot touch '/captures/native-write': Permission denied
native_write_denied
```

The telemetry image seeds `/captures` as Node UID/GID 1000 with mode 0750. Native services run as UID/GID 65532. Conversely, when a native image initializes the volume, telemetry cannot write; an isolated full Compose smoke happened to take this second path, and capture discovery still worked because telemetry currently only reads.

**Expected:** Volume initialization order must not affect native capture production. The collector should have only the read access it requires.

**Actual:** One valid initialization order denies native writes; the opposite order grants telemetry read but relies on incidental world-read permissions. Compose does not encode a deterministic ownership reconciliation step.

**Impact:** Enabling `GRAPHX_CAPTURE_ENABLED=true` can silently or noisily lose capture output depending on which image initializes a fresh volume. The declared hardening model is not portable across service creation orders.

**Required remediation:** Define a shared capture GID and compatible directory/file modes, add both image users to that group, and make initialization deterministic (for example, a narrowly scoped init service or pre-created volume contract). Mount the volume read-only in telemetry once writer compatibility is established. Do not use world-writable permissions.

**Missing regression:** Create a fresh volume in each possible initializer order, run all three native writers, verify collector reads/downloads, inspect ownership/modes, and repeat the Compose deployment from a truly empty project.

### P2 — HTTP rate-limit state is not bounded at the documented 2,048 entries

**Location:** `apps/telemetry/server.mjs:243-253`; claim in `phase_5_handoff.md:85` and security documentation.

**Evidence:** Every new `scope:remoteAddress` is inserted before the size check. When the map exceeds 2,048, the cleanup loop deletes only entries whose window has expired. If more than 2,048 source addresses make requests inside the active 60-second window, no entries are removed and the map continues growing.

**Expected:** An explicit hard cap with deterministic eviction, while retaining clear behavior for active clients.

**Actual:** Expiry is implemented, but the maximum-size claim is false during a sufficiently distributed request burst.

**Impact:** The public HTTP boundary has attacker-influenced, unbounded process state and violates the work package's explicit bounded-resource invariant. Distributed clients can increase memory until entries expire.

**Required remediation:** Enforce a hard maximum after expiry cleanup using deterministic oldest/LRU eviction, or use a fixed-capacity limiter. Keep observation and control scopes separate enough that general traffic cannot evict control policy unexpectedly. Export bounded rejection/eviction metrics in Phase 6 without exposing high-cardinality addresses.

**Missing regression:** Inject more than 2,048 distinct client keys within one window through a test seam, assert the map never exceeds the cap, and test expiry/eviction/control-scope behavior using a fake clock.

### P2 — TLS read/write loops poll the wrong readiness after cross-direction `WANT` results

**Location:** `src/tcp_transport.cpp:182-253`.

**Evidence:** `read_all` always waits for `POLLIN` at line 191. If `SSL_read_ex` returns `SSL_ERROR_WANT_WRITE`, line 208 merely continues, and the next iteration waits for `POLLIN` again. `write_all` similarly always waits for `POLLOUT` at line 229 even after `SSL_write_ex` returns `SSL_ERROR_WANT_READ` at line 245. The handshake loop correctly selects readiness from the actual SSL error (`src/tcp_transport.cpp:321-325`), demonstrating the intended pattern.

**Expected:** Retry readiness follows the last OpenSSL result: `WANT_READ` → `POLLIN`, `WANT_WRITE` → `POLLOUT`, while preserving the original operation and deadline/cancellation semantics.

**Actual:** Cross-direction requirements are ignored. A receive without a deadline may wait indefinitely for unrelated readability; a send may time out even though the needed input was available.

**Impact:** TLS post-handshake processing (including protocol-driven writes/reads such as key updates) can stall otherwise healthy graph edges. Positive round trips do not exercise this state transition.

**Required remediation:** Carry a per-operation desired poll event initialized to the natural direction and update it from each `SSL_get_error` result. Continue honoring `SSL_pending`, send deadlines, receive deadlines, close cancellation, and mutex/session lifetime.

**Missing regression:** Add an OpenSSL test seam or integration scenario that forces `SSL_read_ex` to return `WANT_WRITE` and `SSL_write_ex` to return `WANT_READ`, then prove progress, timeout accuracy, and cancellation.

### P2 — Phase 5 browser security paths have no automated frontend test target

**Location:** `web/package.json`; changed behavior in `web/src/useTelemetry.js:3-38`, `web/src/App.jsx:19-29,83-95`, and `web/src/components/EdgeInspector.jsx:38-45`.

**Evidence:** `web/package.json` defines only `dev`, `build`, and `preview`. Independent `npm test` fails with `Missing script: "test"`. The portable suite builds the frontend and tests server HTTP behavior, but does not execute the browser code that stores the observation token, constructs the authenticated WebSocket protocol, authenticates reset, or downloads captures through bearer-authenticated fetch.

**Expected:** Risk-proportional tests for the browser credential and control paths changed by Phase 5.

**Actual:** Compilation is the only automated frontend evidence.

**Impact:** Regressions can silently remove observation authorization headers, expose the control token to persistent storage, fail WebSocket authentication, or revert capture downloads to unauthenticated links while CI remains green.

**Required remediation:** Add a lightweight frontend test setup and cover session-only observation storage, no persistent control storage, authenticated fetch/WebSocket protocol generation, reset/control bearer use, capture fetch/blob behavior, token changes/reconnect cleanup, and failed-auth UI behavior. Add the test command to the portable CI job.

### P3 — Verifier prompt points to the Phase 4 handoff

**Location:** `prompt/verifier.md:7-11`.

**Evidence:** The prompt selects Phase 5 but names `phase_4_handoff.md`. The actual Phase 5 handoff is `phase_5_handoff.md` and was used for this verification.

**Impact:** A literal verifier can inspect the wrong implementation report and produce stale traceability.

**Remediation:** Change the handoff pointer to `phase_5_handoff.md` when preparing the Phase 5 reverification prompt.

### P3 — Container build inputs are not fully reproducible

**Location:** `Dockerfile:1-13`; `docker/telemetry.Dockerfile:1-12`.

**Evidence:** Base images use mutable tags; apt packages are unpinned; the web build stage uses `npm install` rather than `npm ci`. JavaScript lockfiles exist and current audits pass, so this is not an immediate vulnerability finding.

**Impact:** Rebuilding the same source later can select different OS/runtime/package content, complicating incident reconstruction and release provenance.

**Remediation:** Pin release images by digest, use `npm ci` for the web stage, record SBOM/provenance in the later release-engineering phase, and define an intentional base-image update process.

## 4. Tests and checks run

All commands ran from `/Users/rklinkhammer/workspace/graphx-docker` on macOS arm64 with AppleClang 21, OpenSSL 3.6.3, Node 26.8.1, npm 11.19, and Docker Desktop. Existing user containers were not stopped, removed, or recreated.

| Command / check | Result |
|---|---|
| Clean Debug C++23 configure/build in `build/phase5-verifier-clean`; complete CTest | PASS — 12/12 |
| Clean Release C++20 configure/build in `build/phase5-verifier-cxx20`; complete CTest | PASS — 12/12 |
| Clean ASan/UBSan configure/build in `build/phase5-verifier-sanitizers`; complete CTest | PASS — 13/13, including sanitizer coverage audit |
| `GRAPHX_BUILD_DIR=... scripts/test-features.sh portable` with separate C++20 directory | PASS — C++23 12/12, C++20 12/12, all checked-in configs/dry-runs, TCP/shared-memory pipelines, SIGTERM, PCAPNG/extcap, telemetry tests, web build, authenticated real pause/resume, HTTPS/API checks |
| `npm test` in `apps/telemetry` | PASS — 4/4 |
| `npm audit --audit-level=high` in telemetry and web | PASS — 0 vulnerabilities in each |
| `npm run build` in `web` | PASS — 1,750 modules; 1.846 MB JavaScript chunk warning |
| `npm test` in `web` | FAIL/NOT PROVIDED — no test script |
| Fresh CMake build with `GRAPHX_ENABLE_CPPCHECK=ON` | PASS — production, apps, and tests checked |
| `scripts/run-static-analysis.sh` | NOT RUN TO COMPLETION — exact prerequisite `clang-tidy` is absent |
| `scripts/check-format.sh` | NOT RUN TO COMPLETION — gate requires clang-format 18; host has 23.1.0 |
| `GRAPHX_FUZZ_SECONDS=5 scripts/run-fuzz.sh` | ENVIRONMENT BLOCKED — Apple toolchain lacks `libclang_rt.fuzzer_osx.a`; fuzz targets could not link |
| `git diff --check` | PASS |
| `docker compose config --quiet` | PASS |
| `docker compose build sink telemetry` | PASS — native GNU C++/OpenSSL 3 image and telemetry image built |
| Isolated four-service Compose smoke with capture enabled | PARTIAL PASS — pipeline generated three PCAPNG files and collector listed them; runtime hardening present; isolated project cleaned afterward |
| Deterministic fresh-volume ownership test (telemetry initializes first) | FAIL — native UID 65532 cannot write telemetry-owned UID/GID 1000 mode 0750 volume |
| Raw malformed request-target adversarial test | FAIL — `GET http://[` terminates Node telemetry process |
| Existing-container preservation check before and after | PASS — `gx-ipv-side-sink-1`, `gx-ipv-side-transform-1`, and `gx-ovs-ovs-router-1` remained running |

## 5. Unverified areas and environmental restrictions

- Exact clang-format 18 formatting and clang-tidy 18 analysis: required executables are not installed locally. The modified workflow has not run remotely.
- Fuzz execution: local AppleClang is missing the libFuzzer runtime archive, so targets failed at link time before fuzzing. CI is configured to use clang-18 but has not run for these uncommitted changes.
- Native Linux TLS cancellation/reconnect behavior and the Ubuntu CI matrix: this host is macOS; no remote workflow evidence exists.
- Privileged native-Linux macvlan, ipvlan, OVS, netns, nftables, and netem runtime paths: unavailable on Docker Desktop and outside the Phase 5 code changes. Configuration and dry-run plans passed, but behavior is not reverified here.
- Separate untrusted-CA and missing-client-certificate GraphX negative tests: the shipped TLS smoke covers mTLS success, peer-name mismatch, and handshake cancellation, not these two trust failures.
- HTTP client-certificate authentication and authenticated WebSocket accept/reject in a real browser: code-inspected, not runtime-verified.

These restrictions do not cause a `BLOCKED` verdict because enough required verification was feasible to reach definitive P1/P2 findings.

## 6. Compatibility, architecture, and security assessment

### Compatibility and architecture

- Graph semantics remain independent of Docker, Compose, OpenSSL policy, and the GUI. TLS is correctly modeled as an additive TCP endpoint option and projected through the transport factory.
- Configuration version remains 1. Existing plaintext configurations remain valid, and the protocol/framing/mixed-version tests all pass.
- The four transport contracts and typed receive outcomes remain intact. No Docker or telemetry-vendor dependencies were introduced into the graph model.
- Phase 6–8 systems were not prematurely added. The health endpoint remains liveness-only, telemetry history remains in memory, and bearer credentials are shared capabilities rather than a falsely labeled authorization system.

### Security

- HMAC verification occurs before telemetry state mutation/control endpoint registration; replay and freshness caches are bounded; bearer comparison uses constant-time primitives after equal-length checks.
- Observation and mutation credentials are separated, and reset now follows the authenticated control path. Origin checking is applied to state changes; WebSocket and capture access honor observation credentials when configured.
- Filename allowlists and fixed-root resolution prevent the obvious capture/static traversal paths inspected.
- TLS 1.3, SNI, hostname checks, client-certificate flags, and handshake cancellation are present. Application-data readiness handling must be fixed before TLS can be considered resilient.
- The malformed-target crash is an unauthenticated availability vulnerability at the primary changed HTTP boundary.
- Rate-state growth contradicts the declared hard bound and must be corrected rather than merely documented as expiry-based.
- Container hardening flags are materially improved, but the capture-volume identity contract is incomplete and breaks legitimate writers under one initialization order.

## 7. Documentation drift and test quality

- `phase_5_handoff.md` claims request-target handling and rate state are bounded. The malformed URL crash and non-enforced 2,048-entry maximum invalidate those claims.
- `docs/security.md` says only the named capture volume is writable and describes telemetry ownership, but it does not explain the incompatible native UID or initialization-order requirement.
- The TLS smoke uses one certificate/key as both server and client identity. It proves a positive mTLS path but cannot detect several trust-role mistakes; separate CA, server, and client identities are needed.
- TLS reconnect and post-handshake cross-direction readiness are not covered.
- Server security tests import helper functions only; they do not instantiate the actual HTTP/upgrade boundary, which is why the uncaught URL exception escaped detection.
- Frontend security behavior is entirely untested.
- The exact verifier prompt handoff pointer is stale.

## 8. Required remediation before acceptance

1. Catch malformed HTTP and WebSocket targets, add raw adversarial process-survival tests, and verify health after each case.
2. Establish a deterministic shared capture-volume UID/GID/mode contract; prove both initialization orders; mount telemetry read-only.
3. Enforce the documented hard cap on rate-limit state and test it through an injectable clock/client-key seam.
4. Correct TLS read/write poll-direction transitions for all `SSL_ERROR_WANT_*` outcomes and add forced-transition regression tests.
5. Add browser tests for observation/control/capture security behavior and run them in portable CI.
6. Add independent untrusted-CA, missing-client-certificate, TLS reconnect, and HTTP mTLS negative/positive coverage.
7. Run clang-format 18, clang-tidy 18, the bounded fuzz jobs, Ubuntu/macOS CI, and a clean Linux container/runtime check after remediation.
8. Correct the verifier handoff pointer and update the handoff/security documentation so every limit and volume contract matches implementation.

## 9. Readiness for the next work package

**Not ready for Phase 6.** Phase 6 should begin only after this report's P1/P2 findings are remediated and Phase 5 receives an `ACCEPTED` independent verdict. The eventual Phase 6 work must preserve the additive TLS/configuration compatibility boundary, secret-safe diagnostics, bounded non-blocking telemetry, observation/control credential separation, non-root container baseline, and transport-neutral core architecture.

## Appendix A — Independent evidence notes

- Repository status was already dirty with the Phase 5 implementation plus user-owned prompt changes. Verification did not alter production code or user prompt files; this report is the only verifier-owned source artifact.
- The implementer handoff was treated as a claim set. Its command history was not counted as independent pass evidence unless rerun here.
- The isolated Compose resources and deterministic test volume were removed after their checks. No existing container was modified.
- Build outputs remain under ignored `build/` directories for reproducibility and can be discarded normally.

