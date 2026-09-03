# Phase 8 independent verification report

Date: 2026-09-03  
Repository: `/Users/rklinkhammer/workspace/graphx-docker`  
Branch and inspected base: `main`, `cd7b46f7584336a6c8e09467df7c37e48d759c21`

## 1. Verdict

**ACCEPTED**

Phase 8 satisfies `prompt/implement.md` and `prompt/verifier.md`. The prior P2
restart-boundary credential-redaction defect is closed. A deployment-owned,
bounded previous-credential manifest reconstructs the redaction-only overlap
before the collector accepts work after restart, while only current credentials
authenticate.

The verifier independently reproduced the post-restart condition against the
real collector and SQLite history path. Old HTTP and HMAC credentials were
rejected, new credentials authenticated, exact old values were absent from
snapshot, history, audit, capture-linked state, and process logs, and redaction
was observed. The checked-in integration test additionally covers WebSocket and
OTLP output directly.

All fresh native, sanitizer, telemetry, web, portable, pinned Linux quality,
bounded fuzz, Compose, Prometheus, dependency-audit, syntax, JSON, and
whitespace gates passed. No P0, P1, P2, or P3 finding remains. Phase 8 is ready
to serve as the Phase 9 foundation, subject to the invariants in Section 9.

## 2. Executive summary

Phase 8 provides an opt-in authorized runtime control plane without changing
the transport-neutral GraphX data plane:

- named principals receive bounded action and node scopes;
- observation, control, shared telemetry, and per-node runtime credentials are
  distinct and cross-domain reuse fails closed;
- commands have UUID correlation, actor-scoped idempotency, bounded state,
  delivery/timeout semantics, signed target acknowledgements, and attributable
  live and durable audit;
- the native generator demonstrably pauses and resumes over the real TCP path;
- arbitrary acknowledgement errors are reduced to allow-listed codes;
- accepted telemetry is normalized, bounded, and credential-filtered before
  snapshot, WebSocket, OTLP, capture-reference, and SQLite fan-out;
- credential rotation uses current-only authentication plus a bounded filter
  for candidate, current, in-process retired, and projected previous values;
- the previous-value projection is restart-safe, absolutely expiring,
  permission/size/count constrained, and fail-closed when invalid;
- the GUI exposes pending, accepted, rejected, timed-out, and status-error
  states without persisting the control bearer; and
- the telemetry container runs as `node` with reduced Compose privileges.

The remediation leaves ownership of old raw credential material with the
deployment rather than creating a collector-managed secret journal. The
checked-in example manifest is intentionally expired; operators generate an
active manifest for each coordinated rotation.

## 3. Acceptance-criteria traceability matrix

| Requirement | Implementation evidence | Validation evidence | Status | Remediation |
|---|---|---|---|---|
| Named principals | `apps/telemetry/control.mjs`; policy example | Control unit/integration suites | Implemented | None |
| Action/node authorization | `ControlAuthorizer.permits()`; server routes | Allow/deny/unknown/cross-principal cases | Implemented | None |
| Observation/control separation | Separate bearer and policy paths | Authorization, UI, collision tests | Implemented | None |
| Bounded fail-closed loaders | Protected readers; registry invalidation | Missing/malformed/oversize/duplicate/recovery tests | Implemented | None |
| Per-node runtime identities | Runtime manifest and node HMAC lookup | Wrong-node/endpoint/replay/freshness cases | Implemented | None |
| Cross-domain uniqueness | Fingerprints across all credential roles | Every supported role pairing tested | Implemented | None |
| Atomic file rotation | Forced pre-fan-out reload; retired/previous sets | Candidate/old/new live integration | Implemented | None |
| Restart-safe 60-second filtering | `PreviousCredentialStore`; absolute expiry | Checked-in restart test and independent reproduction | Implemented | None |
| Current-only authentication | Previous values never enter authorizers | Old bearer and old HMAC rejected post-restart | Implemented | None |
| Bounded transition | 64 KiB manifest; 4 KiB secrets; 4,096 entries; 32-byte minimum | Boundary/expiry/permission/capacity tests | Implemented | None |
| Invalid transition fails closed | Combined trust invalidation | Readiness/control/ingestion tests | Implemented | None |
| Real pause/resume | Native generator signed-ACK endpoint | Portable TCP acceptance | Implemented | None |
| Authentic ACK | Node signing, endpoint binding, correlation | Unit/integration adversarial cases | Implemented | None |
| ACK confidentiality | Central sanitizer/error allowlist | Command/audit/history/log checks | Implemented | None |
| Correlation/idempotency | UUID ledger and actor/key/fingerprint | Replay/conflict/stale/wrong-target tests | Implemented | None |
| Bounded retained state | Request/datagram/ledger/audit/history/rate limits | Boundary and overflow suites | Implemented | None |
| Secret-free fan-out | Sanitizer before all consumers | Restart and multi-consumer tests | Implemented | None |
| Secret-free reason/audit | Reason checked before insertion | Old reason rejected with HTTP 400 post-restart | Implemented | None |
| Durable privileged audit | SQLite `control_audit`; `audit:read` | Persistence/restart/access tests | Implemented | None |
| Reset semantics | Collector-local reset | Integration and documentation review | Implemented | None |
| Metrics/alerts/dashboard | Prometheus and Grafana artifacts | Pinned Prometheus tests; JSON parse | Implemented | None |
| GUI lifecycle | `ControlCommandStatus.jsx` | 9/9 web tests | Implemented | None |
| Schema parity | C++ model, JSON schema, topology | Native and portable config validation | Implemented | None |
| Default compatibility | Opt-in control; legacy paths | C++20/23, TCP, SHM, TLS, shutdown | Implemented | None |
| Hardened deployment | Non-root/read-only/reduced privileges | Compose render/build/health/image inspect | Implemented | None |
| Rotation deployment | Rotation overlay and example manifest | Seven-secret active-manifest deployment | Implemented | None |
| Exact Linux quality | Versioned Clang tools, cppcheck, fuzzers | Fresh pinned quality image | Implemented | None |
| Phase isolation | No Phase 9 dissector/extcap work | Repository inspection | Implemented | None |
| Documentation accuracy | Control/security/history/test/handoff docs | Claims checked against runtime | Implemented | None |

Status vocabulary follows the verifier contract. No criterion is Partial,
Missing, or Not Yet Applicable.

## 4. Findings

No P0, P1, P2, or P3 findings were identified.

### Closure of the prior P2

The prior report showed that an in-memory retired set was lost on collector
restart. The remediation closes that lifecycle boundary:

- `apps/telemetry/control.mjs:265-317` strictly parses and bounds the previous
  manifest and protected secret files;
- `apps/telemetry/control.mjs:319-372` loads an absolute-expiry,
  redaction-only `PreviousCredentialStore` at startup;
- `apps/telemetry/control.mjs:403-454` validates current and previous roles
  together, permits same-role staging, and rejects cross-role reuse;
- `apps/telemetry/server.mjs:45-65` selects mutually exclusive models and
  `apps/telemetry/server.mjs:116-135` validates the registry before serving;
- `apps/telemetry/server.mjs:361-375` blocks current/previous values in reasons;
- `apps/telemetry/server.mjs:805-822` refreshes before authentication and
  before fan-out; and
- `apps/telemetry/control.integration.test.mjs:445-578` rotates and restarts
  inside the overlap, covers snapshot/WebSocket/OTLP/capture/history/audit/logs,
  verifies fail-closed malformed state, and verifies recovery.

The independent verifier also started the real collector directly in the
post-restart state with new current credentials and a live previous manifest:

```json
{"ready":true,"oldAuthenticationRejected":true,
 "retiredReasonRejected":true,"oldAdminAbsent":true,
 "oldRuntimeAbsent":true,"oldSignedEventRejected":true,
 "redactionObserved":true,"logsClean":true}
```

This falsifies the old failure mode: previous values participated in redaction
but not authentication and remained absent from retained output and logs.

## 5. Tests and checks run

Commands ran from the repository root unless shown otherwise.

### Environment

```text
macOS Darwin 25.6.0 arm64
CMake 4.4.3; AppleClang 21.0.0.21000101
Node.js v26.8.1; npm 11.19.0
Docker 29.4.0; Docker Compose v5.1.2
```

The worktree already contained the Phase 8 implementation. Verification
preserved it and updated only this report. Generated build outputs and the new
Linux evidence log are verifier artifacts.

### Fresh native configure, build, and CTest

```sh
cmake -S . -B /tmp/graphx-phase8-native.vO5G6Z -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/graphx-phase8-native.vO5G6Z --parallel
ctest --test-dir /tmp/graphx-phase8-native.vO5G6Z --output-on-failure
```

**Pass:** fresh AppleClang/OpenSSL 3.6.3 build; **12/12** in 5.01 seconds,
including TLS security, boundaries, mixed-version, and lifecycle stress.

### Sanitizers

```sh
cmake --preset sanitizers
cmake --build --preset sanitizers --parallel
ctest --preset sanitizers --output-on-failure
```

**Pass:** **13/13** in 8.65 seconds, including sanitizer coverage policy.

### Telemetry and web

```sh
(cd apps/telemetry && npm test && npm audit --omit=dev)
(cd web && npm test && npm run build && npm audit --omit=dev)
```

**Pass:** telemetry **60/60** (~5.58 seconds); web **9/9** plus production
build; both audits reported zero vulnerabilities. Vite retained its non-fatal
approximately 1.85 MB uncompressed chunk warning.

### Cumulative portable suite

```sh
scripts/test-features.sh portable
```

**Pass:** C++23 **12/12** in 4.18 seconds and C++20 **12/12** in 4.13 seconds;
all topologies, finite TCP/shared memory delivery, coordinated shutdown,
telemetry 60/60, web 9/9/build, real TCP pause/resume, HTTPS, authentication,
methods, and secure-bind checks passed. Final output: `Portable feature suite passed`.

### Exact Linux quality gate

```sh
GRAPHX_FUZZ_SECONDS=3 scripts/test-linux-container.sh quality
```

**Pass:** freshly rebuilt pinned Ubuntu 24.04 aarch64 image;
`clang-format-18` checked 37 C++ files; `clang-tidy-18` and cppcheck passed;
sanitizer coverage found 12 library, four application, and two fuzzer units;
both bounded libFuzzer targets passed. Evidence:
`outputs/linux-container/quality-20260903T205345Z.log`. The known non-fatal
external-symbolizer warning appeared.

### Compose render, build, health, and cleanup

With seven independently generated 256-bit files and a fresh 55-second
transition manifest:

```sh
docker compose -p graphx-phase8-reverify \
  -f compose.yaml -f compose.control.yaml \
  -f compose.control-rotation.yaml config --quiet
docker compose -p graphx-phase8-reverify \
  -f compose.yaml -f compose.control.yaml \
  -f compose.control-rotation.yaml build telemetry generator transform sink
docker compose -p graphx-phase8-reverify \
  -f compose.yaml -f compose.control.yaml \
  -f compose.control-rotation.yaml up -d --wait
```

**Pass:** all four services became healthy/running; image inspection returned
`TELEMETRY_USER=node`; containers, network, volume, and secrets were removed.
The build retained known non-fatal GCC 12 `-Wrestrict` diagnostics in
`src/observability.cpp`; exact Clang quality gates were clean.

### Prometheus, syntax, JSON, and whitespace

```sh
docker run --rm --entrypoint promtool \
  -v "$PWD/deploy/observability:/etc/prometheus:ro" \
  prom/prometheus:v3.13.0@sha256:c6b27ea434f8389bfe233fbc7be381cf50587c286e871bc842008f5a1b1908a7 \
  check config /etc/prometheus/prometheus.yml
docker run --rm --entrypoint promtool \
  -v "$PWD/deploy/observability:/etc/prometheus:ro" \
  prom/prometheus:v3.13.0@sha256:c6b27ea434f8389bfe233fbc7be381cf50587c286e871bc842008f5a1b1908a7 \
  check rules /etc/prometheus/alerts.yml
docker run --rm --entrypoint promtool \
  -v "$PWD/deploy/observability:/etc/prometheus:ro" \
  prom/prometheus:v3.13.0@sha256:c6b27ea434f8389bfe233fbc7be381cf50587c286e871bc842008f5a1b1908a7 \
  test rules /etc/prometheus/alerts.test.yml
find apps/telemetry -type f \( -name '*.mjs' -o -name '*.js' \) \
  -print0 | xargs -0 -n1 node --check
git diff --check
```

**Pass:** one Prometheus config/rule file and eight rules were valid; all rule
tests passed. JavaScript syntax, GraphX schema, Grafana dashboard, previous
manifest example JSON, and whitespace passed. An initial verifier JSON command
used `schema/` instead of `config/schema/`; the corrected parse passed. This was
a verifier path error, not a product failure.

### Independent restart adversarial test

```sh
node /tmp/graphx-phase8-restart-overlap-verifier.mjs
```

**Pass:** JSON evidence is in Section 4. The script was outside the repository
and removed after use.

## 6. Items not runtime-verified

- Privileged Linux OVS, macvlan, ipvlan, and network-namespace labs were not
  rerun on macOS. They are outside portable Phase 8 control-plane acceptance.
- No multi-hour soak, production-scale rotation, high-rate UDP benchmark,
  external penetration test, or manual browser accessibility session ran.
- External OTLP interoperability beyond controlled receiver/mTLS tests and an
  operator-specific secret manager/backup integration were not exercised.

These are environmental or later hardening activities, not unverified Phase 8
criteria. The restart boundary, Compose model, native runtime, and pinned Linux
quality path were runtime-verified.

## 7. Compatibility and security assessment

Runtime control remains opt-in and transport-neutral. C++20/23, default and
legacy configuration, TCP, shared memory, TLS, finite delivery, shutdown, and
native pause/resume pass. Phase 8 contains no Phase 9 dissector/extcap scope.

Only current credentials authenticate. Current, candidate, retired, and
explicitly previous values are filtered before retained/distributed telemetry.
Cross-domain reuse fails closed, including during transition. Previous files
are deployment-owned, protected, bounded, and governed by a canonical absolute
expiry no more than 60 seconds ahead. Invalid state disables readiness, control,
and authenticated ingestion without logging secrets.

The documented publication order remains operationally significant: publish
protected previous values, publish the active manifest, atomically replace
current credentials, restart affected runtimes, then remove the projection
only after expiry.

## 8. Required remediation summary

None. Phase 8 has no acceptance-blocking remediation.

Non-blocking future work may include a longer rotation soak, manual UI and
accessibility testing, production secret-manager examples, and removal of the
existing Vite chunk-size and GCC 12 diagnostic noise.

## 9. Next-phase readiness

**Ready.** Phase 9 may begin on this accepted baseline. It must preserve:

1. opt-in, transport-neutral runtime control;
2. separate trust domains and fail-closed cross-domain reuse checks;
3. current-only authentication plus bounded restart-safe redaction-only
   previous values through absolute expiry;
4. normalization, bounds, and credential filtering before every current or
   future snapshot/WebSocket/OTLP/capture/history/audit/dissector fan-out;
5. command correlation, actor idempotency, signed target ACK, endpoint binding,
   timeout, bounded retention, and privileged audit;
6. no credential-exfiltration or unauthenticated-control surface through
   capture, PCAPNG, dissector, or extcap additions; and
7. default C++20/23, legacy, TLS, TCP/shared memory, non-root deployment, and
   pinned Linux quality compatibility.

## Appendix A — Evidence map

| Evidence | Path |
|---|---|
| Contracts | `prompt/verifier.md`; `prompt/implement.md` |
| Handoff | `phase_8_handoff.md` |
| Credential registry | `apps/telemetry/control.mjs` |
| Server paths | `apps/telemetry/server.mjs` |
| Restart integration | `apps/telemetry/control.integration.test.mjs` |
| Boundary tests | `apps/telemetry/control.test.mjs` |
| Normalization/history | `apps/telemetry/security.mjs`; `apps/telemetry/history-worker.mjs` |
| Native control | `apps/common.hpp` |
| GUI | `web/src/components/ControlCommandStatus.jsx`; `web/src/control-status.test.mjs` |
| Deployment | `compose.yaml`; `compose.control.yaml`; `compose.control-rotation.yaml`; `docker/telemetry.Dockerfile` |
| Transition example | `config/previous-credentials.example.json` |
| Monitoring | `deploy/observability/alerts.yml`; `deploy/observability/alerts.test.yml`; Grafana dashboard |
| Procedures | `docs/control-plane.md`; `docs/security.md`; `docs/history.md`; `docs/test-procedure.md` |
| Linux evidence | `outputs/linux-container/quality-20260903T205345Z.log` |

## Appendix B — Verification hygiene

- No production source, configuration, deployment, schema, test, or operator
  documentation file was changed by this verification pass.
- Only `phase_8_verification.md` was updated.
- Temporary Compose resources and credential files were removed.
- The verifier-only adversarial script was removed.
- Existing implementation and unrelated worktree changes were preserved.
