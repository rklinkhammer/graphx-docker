# Phase 8 implementation handoff

Date: 2026-09-03

Work package: authorized control plane and real runtime controls

## 1. Outcome summary

Phase 8 is implemented. GraphX now has a least-privilege, attributable and
bounded control plane for real source pause/resume and collector-local counter
reset. A versioned local policy maps named principals to allowed actions and
topology node IDs. Commands have UUID identity, actor-scoped idempotency,
explicit expiry, delivery count, per-node authenticated acknowledgements and
distinct `pending`, `accepted`, `rejected`, and `timed-out` outcomes.

Runtime actions reuse the Phase 5 HMAC telemetry channel but no longer infer
success from UDP delivery. The source validates the HMAC envelope, freshness,
nonce, command kind, UUID, exact node target and expiry before changing its real
production pause flag. It then returns a signed acknowledgement with the same
command UUID and resulting runtime state. The collector accepts that ACK only
from the live UDP endpoint to which the target node is registered and only when
command, action and target all correlate. In policy mode, each topology node has
a distinct HMAC identity and receives only its own secret; the collector verifies
the claimed node with that node's key before it can replace a live endpoint.

The implementation includes strict typed limits in the authoritative YAML
model and JSON schema, compatibility routes, a policy/secret Compose overlay,
bounded in-memory command and audit ledgers, optional durable audit through the
Phase 7 history worker, web-console command identity/status, Prometheus metrics,
Grafana panels, alerts, an ADR, operator documentation and adversarial tests.
Signed runtime input is treated as authenticated but untrusted: negative ACK
errors retain only documented protocol codes and all arbitrary text is replaced
before it reaches command responses, audit, durable history, or logs.
The final remediation adds one collector-owned registry that atomically checks
non-reversible fingerprints across observation, control, shared-HMAC, and
per-node runtime credentials. It also normalizes every accepted telemetry event
before fan-out, bounds free-form diagnostics, allowlists state values, removes
unknown fields, and redacts active, candidate, and recently superseded
credentials from every retained string. The
latest verifier remediation coordinates file-only rotation with that boundary:
every valid signed event forces a combined credential reload before fan-out,
and superseded values remain in a capped redaction-only registry for 60 seconds.
The restart-boundary remediation adds an optional deployment-projected,
strictly bounded previous-credential manifest. It restores old redaction values
before readiness after collector restart, applies an absolute expiry of at most
60 seconds, authenticates only the current snapshot, and fails closed on unsafe
permissions, malformed input, excessive size/count, or cross-role reuse. The
collector does not create a durable raw-secret journal.

No Phase 9 PCAPNG/dissector/extcap behavior was added. The pre-existing user
changes to `prompt/implement.md` and `prompt/verifier.md` were preserved and are
not part of this implementation.

### Verification-report remediation

This revision addresses every required change in `phase_8_verification.md`:

| Finding | Correction and evidence | Status |
|---|---|---|
| Shared runtime HMAC allowed endpoint takeover | Policy mode now requires a complete unique per-node identity manifest; the live integration test signs a generator-claiming heartbeat and ACK with the transform key and proves both are rejected | Closed |
| Free-form reason could persist a configured credential | Issue validation compares the reason against observation, legacy HMAC, every current operator token, and every per-node identity before ledger/audit insertion | Closed |
| Observation could inspect command summaries and durable control audit | Snapshot command summaries were removed; `/api/history` excludes `control_audit`; `/api/control/audit/history` requires explicit `audit:read` | Closed |
| Browser remained at initial pending response | Mounted `ControlCommandStatus` polls the actor-authorized UUID resource with bounded lifetime and stops on accepted/rejected/timed-out | Closed |
| Exact clang-format-18 gate failed | The reported C++ lines were formatted with clang-format-18 and the exact 37-file gate passes | Closed |
| Linux quality wrapper relied on absent `.git` metadata | Format inventory now uses the copied repository source roots; the complete quality mode passes in the freshly rebuilt verifier image | Closed |
| Runtime ACK error could expose an HMAC credential | A centralized ACK sanitizer retains only five non-secret protocol error codes and maps every arbitrary value to `runtime-rejected` at both the UDP and ledger boundaries; unit and live restart tests cover every credential class across responses, audit, history, snapshots, and logs | Closed |
| Cross-domain credential reuse could collapse runtime, observation, and control roles | `CredentialRegistry` compares SHA-256 fingerprints across every configured credential role on startup and reload; any collision clears the control/runtime snapshots, endpoints, and state, returns readiness 503, reports policy/service readiness as 0, and logs only role names. Unit tests cover every supported role pair and rotation recovery; a live collector test reproduces the verifier's three-role reuse and proves reset returns 503 | Closed |
| Ordinary signed telemetry could expose and persist runtime credentials | All accepted telemetry now passes through one event-kind normalization and credential filter before snapshot/WebSocket, OTLP, history, and capture-reference fan-out. Diagnostic text is bounded to 256 characters, connection/backpressure values are allowlisted, unknown fields are removed, and exact/embedded credentials are redacted. Unit and live restart tests cover trace errors, identifiers, capture references, rejected connection states, snapshot output, durable history, and logs | Closed |
| Newly rotated credential could be retained before the throttled refresh | A valid signed event is authenticated against the current snapshot, then forces a bounded combined reload before fan-out. Candidate values are therefore filtered immediately, and superseded values remain redaction-only for a bounded 60-second overlap with a 4,096-value fail-closed cap. Deterministic tests cover overlap/expiry/capacity; a live atomic-file rotation test covers control and runtime values, snapshots, capture references, history restart, audit reasons, authentication, and logs | Closed |
| Collector restart discarded the retired-credential overlap | `PreviousCredentialStore` loads an operator-projected version-1 manifest of protected old-value files before readiness. Its absolute expiry cannot exceed the 60-second overlap, it never authenticates, and it participates in combined capacity/cross-role checks. The live test rotates admin/runtime values, restarts inside the overlap, proves old authentication fails, filters old exact values across snapshot/WebSocket/OTLP/capture/history/audit/logs, rejects an old value in a new-authenticated reason, and exercises malformed-state failure/recovery | Closed |

The verifier's additional control-parser fuzzing note remains a defense-in-depth
recommendation rather than an acceptance blocker. Control HTTP parsing has strict
body/property/type/length bounds and targeted malformed/adversarial integration
coverage; the native envelope and frame parsers remain covered by libFuzzer.

## 2. Requirements and acceptance criteria

| Requirement | Measurable acceptance criterion | Result |
|---|---|---|
| Separate privileged control | Observation bearer cannot invoke or inspect privileged control; each control endpoint requires a control principal | Met |
| Named authorization policy | Versioned policy grants an explicit principal only listed actions on listed topology nodes | Met |
| Safe policy rotation | Token-only or policy-file replacement is re-read within one second for authentication; valid signed telemetry forces a pre-fan-out reload; a bounded expiring previous-value projection preserves redaction across collector restart; invalid current or transition state fails closed and is observable | Met |
| Real runtime control | Pause changes the source's production gate and resume restores it; reset clears only collector aggregation | Met |
| Command identity | Every new command receives a UUID and target/action/actor/issue/expiry metadata | Met |
| Honest outcome semantics | HTTP issue is `pending`; only correlated ACK makes pause/resume `accepted`; rejection and timeout remain distinct | Met |
| Idempotent retry | Same actor/key/fingerprint returns the original command without redelivery; conflicting reuse returns 409 | Met |
| Runtime authenticity and freshness | Policy mode has unique per-node HMAC identities; control and ACK require bounded clock skew, nonce anti-replay, exact kind/target/UUID/expiry; ACK also matches the live endpoint | Met |
| Bounded resources | Body, policy, principals, commands, audits, retention, idempotency TTL, rate and WebSocket/datagram limits are finite | Met |
| Attributable audit | Decisions contain actor/action/target/command/reason; command reasons reject configured credentials and runtime ACK errors retain only non-secret protocol codes before persistence | Met |
| Durable audit option | `control_audit` survives store restart but is available only through the explicitly authorized control-audit route | Met |
| Operations | Metrics distinguish outcomes/denials/pending/policy validity/audit drops; dashboard and alerts cover policy failure/timeouts | Met |
| GUI | Browser uses the command API with a generated idempotency key and polls pending command identity to a terminal accepted/rejected/timed-out display | Met |
| Compatibility | Existing graph/wire formats and observation APIs remain unchanged; bodyless token routes still work | Met |
| Documentation | Policy, deployment, API, state model, limits, failure modes, tests and trust limitations match code | Met |
| Phase isolation | No arbitrary command execution, distributed scheduling, fault-control expansion or Phase 9 implementation | Met |

## 3. Architecture and compatibility decisions

### Ownership and flow

1. `ControlAuthorizer` owns only the currently valid policy snapshot. It reads a
   maximum 64 KiB policy and up to 64 bounded secret files on a throttled
   one-second check. Any read/parse/reference error clears principals and fails
   control closed.
2. `CredentialRegistry` publishes control and runtime snapshots only after one
   cross-domain collision check. A valid signed event forces this bounded reload
   before fan-out. Removed values remain redaction-only for 60 seconds, with a
   hard 4,096-value cap that fails ingestion and control closed rather than
   evicting a still-sensitive value.
3. `PreviousCredentialStore` reads an optional operator-owned 64 KiB transition
   manifest and protected 32–4,096 byte secret files. Values expire no more than
   60 seconds after loading, survive collector restart through deployment
   projection, and are redaction-only. It does not persist or authenticate them.
4. `ControlPlane` owns a bounded in-memory command ledger, actor-scoped
   idempotency cache, audit ring and counters. It has no Docker, GUI, transport
   or storage dependency. An optional bounded audit sink feeds Phase 7 history.
5. `server.mjs` performs HTTP boundary validation, authorization, command target
   resolution and UDP delivery. Reset is explicitly collector-local. Source
   commands remain pending until ACK.
6. `UdpJsonTraceSink` owns the native control listener thread already associated
   with a source runtime. Only validated signed commands change its atomic pause
   flag. The generator's existing production loop observes that flag.
7. Runtime state is derived from accepted per-node ACKs rather than successful
   `send()`. Actor and reason remain in control-authorized command/audit APIs and
   are removed from read-only observation snapshots.

### Policy model

Policy version 1 contains `principals`, each with a bounded identifier,
`token_file`, one or more permissions (`pause`, `resume`, `reset`,
`commands:read:any`, or `audit:read`), and one or more node IDs or `*`. Node
references are checked against the authoritative topology.
Tokens must be unique and 32–4,096 bytes. The checked-in example has paths only;
Compose projects token values as secrets.

Cross-actor command reads and audit reads are independent explicit permissions:
`commands:read:any` and `audit:read`. Non-administrators may query only commands
they issued. Observation snapshots contain no command ledger, and general
history queries always exclude `control_audit`.

### Command protocol

The additive HTTP command body is:

```json
{"action":"pause","targetNodes":["generator"],"reason":"maintenance"}
```

The additive signed UDP payload is:

```json
{"kind":"control","action":"pause","commandId":"UUID","targetNode":"generator","issuedAt":0,"expiresAt":0}
```

The signed ACK is:

```json
{"kind":"control_ack","nodeId":"generator","action":"pause","commandId":"UUID","accepted":true,"state":"paused"}
```

Negative ACKs may use only `busy`, `invalid-state`, `policy-denied`,
`runtime-error`, or `unsupported-action` as a retained `error`. Any other value
is represented as `runtime-rejected`; arbitrary runtime text never becomes part
of the public or durable control record.

This does not change the GraphX envelope wire protocol. UDP telemetry JSON is an
internal additive control contract protected by the already-required HMAC
secret. Unknown, malformed, stale, replayed, expired or misdirected packets are
ignored without changing runtime/control state.

### Compatibility

- Configuration version remains 1; `observability.control` is additive with
  defaults and strict unknown-key rejection.
- `/api/control/pause`, `/resume`, and `/reset` remain bodyless POST routes.
- `GRAPHX_CONTROL_TOKEN` remains supported as `legacy-operator`, with all actions
  and nodes, when a policy file is not selected. This is the only mode retaining
  a shared runtime HMAC trust domain.
- Observation, history, health, transport and envelope behavior is unchanged.
- Policy control requires `GRAPHX_RUNTIME_IDENTITY_FILE` with exactly one unique
  secret for every configured node. Existing unsigned loopback telemetry remains
  supported only when control is disabled.

ADR 0009 records the decision to use a local bounded policy/ledger rather than
an external identity provider, remote orchestrator or database-mediated command
queue. This preserves an understandable deployment while providing the required
production semantics.

## 4. Failure cases and explicit limits

| Boundary | Default / hard behavior |
|---|---|
| Command acknowledgement deadline | 2,000 ms; configurable 100–30,000 ms |
| Command retention | 3,600 s; configurable 60–86,400 s |
| Commands retained | 1,024; configurable 1–10,000; overflow rejects |
| In-memory audits | 4,096; configurable 10–100,000; oldest evicted and counted |
| Idempotency TTL | 3,600 s; configurable 60–86,400 s |
| Command request body | 4 KiB; configurable 256–16,384 bytes |
| Policy | 64 KiB, exactly version 1, at most 64 principals |
| Principal token / node secret | mutually distinct across all GraphX credential roles, 32–4,096 bytes; one trailing newline removed |
| Reason | absent or 1–256 characters; rejected if it contains a configured GraphX credential |
| Runtime ACK error | at most 256 characters on input; only five documented codes are retained, otherwise `runtime-rejected` |
| Runtime diagnostic message | at most 256 characters; connection and backpressure values are allowlisted; credentials are redacted before fan-out |
| Idempotency key | 1–128 restricted characters, scoped to actor |
| Targets | nonempty unique configured source IDs; reset is collector-only |
| State-changing HTTP | existing allowed-origin check and 10/minute/IP control limiter |
| UDP | existing 16 KiB datagram, ±30 s HMAC timestamp and 4,096 nonce bounds |

Failure meanings are intentionally not collapsed:

- 400: malformed action/target/body/key/reason;
- 401: invalid control credential when a valid control policy is available;
- 403: authenticated principal lacks action or target permission;
- 409: idempotency conflict or no currently live target runtime;
- 413/415: body limit or media type violation;
- 429: rate or ledger capacity limit;
- 503: runtime control is disabled or policy has failed closed;
- `rejected`: an authorized target returned a negative ACK;
- `timed-out`: all required positive ACKs did not arrive before expiry.

Timeout is not cancellation. A source may have applied a command whose ACK was
lost; the command correctly remains uncertain/timed-out. Operators should use
the resulting node state and retry with a new idempotency key only after deciding
that repeating the idempotent pause/resume effect is safe.

## 5. Major components changed

| Component | Evidence |
|---|---|
| Policy/runtime-identity parsing, authorization, command/idempotency/audit ledger | `apps/telemetry/control.mjs` |
| HTTP APIs, target delivery, endpoint-bound ACK, state, metrics, history sink | `apps/telemetry/server.mjs` |
| ACK boundary validation and arbitrary-error sanitization | `apps/telemetry/security.mjs` |
| Runtime kind/UUID/target/expiry validation and signed correlated ACK | `src/observability.cpp` |
| Typed configuration and strict schema | `include/graphx/config.hpp`, `src/config.cpp`, `config/schema/graphx.schema.json`, `graphx.yaml` |
| Authorization/unit/adversarial/durable-audit/runtime tests | `apps/telemetry/control.test.mjs`, `apps/telemetry/control.integration.test.mjs`, `apps/telemetry/history.test.mjs`, `apps/telemetry/security.test.mjs`, `tests/test_main.cpp`, `tests/test_config.cpp` |
| Web command helper and terminal status reconciliation | `web/src/auth.js`, `web/src/components/ControlCommandStatus.jsx`, `web/src/control-status.test.mjs`, `web/src/App.jsx` |
| Container, current secrets, and restart-safe previous-value projection | `docker/telemetry.Dockerfile`, `compose.yaml`, `compose.control.yaml`, `compose.control-rotation.yaml`, `config/control-policy.example.json`, `config/runtime-identities.example.json`, `config/previous-credentials.example.json` |
| Metrics panels and alerts | `deploy/observability/alerts.yml`, `deploy/observability/alerts.test.yml`, `deploy/observability/grafana/dashboards/graphx-operations.json` |
| Architecture and operation | `docs/adr/0009-authorized-runtime-control-plane.md`, `docs/control-plane.md`, `docs/security.md`, `docs/observability.md`, `docs/test-procedure.md`, `README.md` |
| Portable compatibility coverage | `scripts/test-features.sh` |

No new production dependency was added.

## 6. Tests and checks run

### Actual runtime verification after verifier remediation

- Fresh AppleClang 21 C++23 configure/build in
  `/tmp/graphx-phase8-implement-native.UgS9cg` passed; complete CTest passed
  **12/12** in 5.01 seconds.
- Development rebuild after the final native command validation change passed;
  CTest passed **12/12**, including real signed pause, resume, wrong-target and
  expired-command behavior in `graphx-tests`.
- ASan/UBSan final rebuild passed; CTest passed **13/13** in 8.91 seconds,
  including sanitizer coverage audit and the final exact-kind/expired-command behavior.
- Cumulative portable feature suite passed: C++23 **12/12** in 4.22 seconds,
  C++20 **12/12** in 4.19 seconds,
  all checked-in topology validation, finite TCP/shared-memory delivery,
  coordinated shutdown, telemetry security/API tests, real TCP generator
  pause/resume, web tests/build and secure HTTP behavior.
- Telemetry tests passed **60/60**. The live collector tests demonstrate that a
  transform key cannot claim the generator identity, receive its command, or
  forge its ACK; configured credentials are rejected as reasons; observation
  cannot retrieve command summaries or durable audit; explicit audit permission
  can retrieve durable audit; and credentials submitted as exact or embedded
  negative-ACK errors are absent from commands, audit, reopened history,
  observation snapshots, and logs.
- After strengthening the final fan-out assertions, the focused live control
  integration suite passed **5/5** and independently observed the post-restart
  marker in snapshot, WebSocket, OTLP, capture-linked state, and SQLite history.
- Cross-domain collision tests cover all supported observation/control/shared
  HMAC/per-node HMAC role pairings, token-only collision and recovery, and the
  verifier's live three-role reuse. The live collector stays unready, reports
  invalid policy/service metrics, disables authenticated ingestion, returns 503
  for reset, and never logs the reused value.
- Ordinary telemetry tests submit exact and embedded credentials through trace
  diagnostics, message/parent/trace/type identifiers, capture references,
  unknown fields, and invalid connection states. Live snapshots and SQLite
  history, including after restart, contain none of the submitted credentials.
- The new live rotation regression atomically replaces an operator token and a
  per-node runtime HMAC inside the one-second poll interval. The candidate and
  superseded values remain absent from snapshots, WebSocket messages, OTLP
  requests, capture-linked recent state, SQLite after restart, and logs. It now
  projects the old values, restarts the collector inside the live overlap,
  submits old exact values under new authentication, proves old authentication
  fails, and rejects an old token from a retained command reason. It also proves
  malformed transition state makes readiness/control fail closed and recovers
  after correction. Deterministic tests cover forced discovery, absolute
  expiry, protected-file permissions, cross-role collision, duplicate values,
  and combined fail-closed capacity.
- Web tests passed **9/9**, including a mounted component that transitions
  `pending` to each of `accepted`, `rejected`, and `timed-out`, proves polling
  stops at terminal state, and bounds an unavailable status request. The
  Vite production build passed and retained
  its existing non-fatal large-chunk warning (~1.85 MB uncompressed).
- The telemetry container rebuilt successfully after final server hardening and
  includes `control.mjs`.
- Base plus `compose.control.yaml` and `compose.control-rotation.yaml`, including
  three current runtime secrets, two control tokens, two previous-value secrets,
  the complete identity manifest, and the expiring transition manifest,
  validated successfully with Docker Compose.
- The updated telemetry and native images rebuilt, and the complete isolated
  overlay started under project `graphx-phase8-implement-rotation`. Telemetry,
  generator, transform, and sink all reached healthy/running status with seven
  independently generated temporary credential files. The project network,
  containers, volume, and temporary credentials were removed after the check.
- Prometheus 3.13 `promtool test rules alerts.test.yml` passed in the pinned
  container, including the new fail-closed control-policy alert cases.
- Telemetry production dependencies and web dependencies each reported
  **0 vulnerabilities** with `npm audit`.
- The first Compose-fixture command used an unsupported local OpenSSL argument
  order and stopped before Compose ran. The corrected credential-generation
  invocation then passed render, build, startup/health, and cleanup; this was a
  test-harness command error, not a product failure.

### Linux pinned quality execution and artifact inspection

- `GRAPHX_FUZZ_SECONDS=3 scripts/test-linux-container.sh quality` passed in the
  pinned Linux verifier image on 2026-09-03. It ran exact `clang-format-18`
  across **37** repository C++ files, `clang-tidy-18` and cppcheck across
  production/application/test targets, sanitizer coverage audit, and both
  libFuzzer targets for three-second smoke windows without a finding. Evidence:
  `outputs/linux-container/quality-20260903T203945Z.log`.
- The formatter finding at `tests/test_main.cpp:1174-1175` was corrected with
  the pinned formatter. `scripts/check-format.sh` now discovers source files
  from repository source roots, so it works in the verifier image where `.git`
  is intentionally absent.
- Node parsed the JSON schema and 15-panel Grafana dashboard.
- `git diff --check` passed.
- No secret value appears in the checked-in policy, APIs, observation snapshot,
  audit tests or documentation examples.
- The verifier's original ACK-error reproduction now reports `false` for command,
  live-audit, and durable-history leakage and stores `runtime-rejected`.
- The verifier's rotation-window sequence now redacts the replacement before
  durable history insertion, while the same replacement becomes the active
  control credential. With the explicit previous-value projection, the old
  value remains filtered after collector restart for the original bounded
  60-second window and never authenticates.

### Host-specific boundary not rerun

- Native macvlan, IPvlan, OVS, netns/router and netem tests were not rerun on
  macOS because those remain Linux-host Phase 1 network acceptance boundaries;
  Phase 8 did not modify them. Their configuration/dry-run portions passed in
  the portable suite.

## 7. Known limitations

- The policy is a local bearer-file model, not OIDC, mTLS principal mapping,
  centralized revocation or distributed authorization.
- Policy mode now has a unique HMAC identity for every runtime node. The legacy
  `GRAPHX_CONTROL_TOKEN` compatibility mode still uses one shared trust-domain
  secret and should be restricted to its documented compatibility use.
- Command and idempotency ledgers are collector-local and intentionally not
  recovered after restart. Durable Phase 7 audit records survive when enabled.
- UDP remains best effort. There is no automatic command retransmission,
  cancellation, leader election or multi-collector consensus.
- `reset` clears live aggregation but deliberately does not delete history or
  captures and does not reset source processes.
- The UI issues commands against the default source set and now reconciles their
  terminal state. The REST API provides precise target selection and audit
  access; a richer role/target administration view is a later enhancement.
- Runtime rejection has full ledger semantics and tests. Its error detail is
  intentionally limited to documented protocol codes; custom free-form runtime
  diagnostics require a separate secret-safe observability channel. The current
  native source only positively accepts valid pause/resume and has no
  business-rule rejection mode beyond ignoring invalid unauthenticated input.
- Direct credential matching prevents accidental or literal secret retention;
  it cannot prevent an authenticated malicious runtime from encoding secret
  material into unrelated syntactically valid operational values. Telemetry and
  history therefore remain sensitive and require the documented access controls.
- Every accepted signed datagram performs one bounded synchronous credential
  reload before fan-out. This cannot block graph execution because telemetry is
  best effort, but deployments with very large identity manifests should
  capacity-test collector throughput.
- Restart safety is explicit rather than automatic: before replacing a current
  value, the operator must project the exact old value and a fresh expiring
  manifest. Restart without that projection cannot reconstruct a secret that
  the collector deliberately never persists. The documented five-step rotation
  procedure is therefore part of the security contract.

## 8. Risks for independent verification

The Phase 8 verifier should pay particular attention to:

1. rotating only a token file while leaving the policy file untouched;
2. replacing the policy with malformed JSON and confirming 503 plus policy
   metric/alert, then restoring it;
3. same-key exact replay versus changed action/target/reason conflict;
4. a heartbeat or ACK signed by the wrong node identity, or an ACK with wrong
   UUID, action, node, UDP endpoint, expiry or replayed nonce;
5. a partial multi-target command where only some ACKs arrive;
6. command/audit capacity and retention boundaries, including idempotency entry
   cleanup when command retention is shorter;
7. exact and embedded credentials in signed negative ACK errors, including the
   command API, live audit, history after restart, snapshots, and process logs;
8. observer snapshots/API responses/history/logs for principal token or HMAC
   secret leakage;
9. collector restart after audit persistence and while a command is pending;
10. the Compose overlay with real restrictive token and per-node identity files
   under the hardened read-only/non-root telemetry image;
11. exact clang-format-18 and clang-tidy-18 gates on the Linux verifier host.
12. every cross-domain credential pair at startup and file-only rotation,
    including readiness/metric behavior and recovery;
13. exact and embedded credentials in every retained ordinary telemetry string,
    including snapshot/WebSocket, OTLP, history restart, capture references,
    oversized diagnostics, invalid state values, and logs.
14. control-token and runtime-HMAC file replacement immediately after a refresh,
    including candidate discovery, old/new overlap, capacity failure, snapshots,
    capture-linked state, history restart, command reasons, and logs.
15. collector restart inside the overlap with the explicit previous-value
    projection, including expiry, permission, malformed-input, cross-role,
    capacity, current-only authentication, and every retained fan-out surface.

## 9. Acceptance checklist

- [x] Concrete requirements, invariants, failure cases and limits were defined.
- [x] Named action/node authorization is enforced independently from observation.
- [x] Policy and token-file rotation is bounded and fail-closed.
- [x] Rotation discovers candidate values before valid event fan-out and filters
  superseded values for a bounded 60-second, 4,096-value overlap.
- [x] An expiring deployment-projected previous-value manifest preserves that
  filtering across collector restart without authenticating or journaling old
  values; invalid transition state fails closed.
- [x] Credential values are mutually distinct across trust domains at startup
  and reload, with non-secret readiness and metric diagnostics.
- [x] Commands have stable unique identity and actor-scoped idempotency.
- [x] Pending, accepted, rejected and timed-out are distinct.
- [x] Actual source pause/resume changes runtime production behavior.
- [x] ACKs are signed and correlated to command/action/node/live endpoint.
- [x] Malformed, forged, stale, replayed, expired and wrong-target inputs fail safe.
- [x] Arbitrary signed ACK errors cannot enter responses, audit, history, or logs.
- [x] Ordinary signed telemetry is bounded, normalized, and credential-filtered
  once before every live, export, capture-reference, and durable consumer.
- [x] Control/audit/request resources have configurable strict bounds.
- [x] Audit is redacted, attributable and optionally durable across restart.
- [x] GUI, metrics, alerts, deployment and documentation match behavior.
- [x] Existing compatibility routes and default-off security behavior remain.
- [x] Unit, integration, adversarial, runtime, sanitizer, portable and container
  coverage is proportional to the boundary.
- [x] No new dependency or Phase 9 implementation was introduced.
- [x] The exact Linux clang-format-18, clang-tidy-18, cppcheck, sanitizer
  coverage, and bounded fuzz quality wrapper passes after remediation.

## 10. Recommended next work package

Run `prompt/verifier.md` independently for Phase 8. Do not begin the next work
package unless it returns `ACCEPTED`.

After acceptance, the next scheduled package is Phase 9: production PCAPNG,
Wireshark dissector and extcap implementation. It must preserve Phase 8's
separate observation/control credentials, credential-filtered bounded audit, exact
command outcome semantics, nonblocking graph behavior, envelope compatibility,
and the existing distinction between application-frame capture and Ethernet/OVS
capture. Phase 9 must not turn capture tooling into an implicit control or secret
exfiltration channel.
