# Phase 5 implementation handoff

Date: 2026-09-01  
Repository: `/Users/rklinkhammer/workspace/graphx-docker`  
Work package: Authentication, TLS, API validation, and container hardening

## 1. Outcome summary

Phase 5 is implemented. GraphX now supports optional TLS 1.3 and mutual TLS on
TCP graph edges, including certificate-chain and peer-name verification. Runtime
telemetry and control datagrams can be authenticated with HMAC-SHA256,
timestamp freshness, random nonces, constant-time comparison, and bounded replay
rejection. The telemetry service supports HTTPS/TLS 1.3 and optional client
certificates, separate observation and control bearer credentials, strict API
methods/origins/bodies/paths/limits, authenticated WebSocket upgrades, bounded
rate state, and safe static/capture resolution.

The browser console can supply an observation token for API, WebSocket, and
authenticated capture downloads. Standard Compose services now use read-only
roots, all-capability drop, `no-new-privileges`, bounded PIDs, init processes,
non-root users, private temporary filesystems, a non-world-writable capture
directory, and host-loopback telemetry publication.

The security work preserved version-1 configuration compatibility, exact
envelope/framing bytes, all four transport contracts, typed receive outcomes,
and the separate privileged-network-laboratory boundary. Existing TCP edges and
loopback telemetry remain plaintext unless security is explicitly configured;
runtime mutation is disabled unless both its bearer and datagram-authentication
secrets are present.

No Phase 6 health/readiness/SLO/dashboard expansion, Phase 7 persistence, or
Phase 8 multi-user authorization/audit system was implemented.

### Post-verification remediation

The first independent Phase 5 verification returned `CHANGES REQUIRED`. The
reported P1/P2 issues have since been remediated in the working tree:

- malformed HTTP targets now return 400, malformed WebSocket upgrades close only
  their socket, and an actual spawned-server regression proves continued health;
- general and control rate state use separate hard-capped 2,048-entry limiters;
- TLS application-data retries poll the readiness direction requested by OpenSSL,
  explicit close sends a best-effort TLS close notification, and the TLS smoke now
  covers reconnect, untrusted CA, missing client certificate, name mismatch, mTLS,
  and cancellation;
- both images seed `/captures` as `65532:65532` mode `0770`, the Node user belongs
  to that group, and Compose mounts the collector view read-only;
- browser credential/control helpers now have an executable test target; and
- the verifier prompt now points to this Phase 5 handoff.

## 2. Requirements and acceptance criteria defined before implementation

### Concrete requirements

1. Add optional TLS 1.3 to TCP edges without changing envelope or framing bytes.
2. Verify certificate chains and endpoint identities by default; support mutual
   TLS and actionable secret-safe failure diagnostics.
3. Separate observation authentication from mutation authentication and require
   every control action, including counter reset, to authenticate.
4. Prevent unauthenticated UDP input from establishing runtime-control endpoints;
   authenticate events, commands, and acknowledgements with freshness/replay
   protection.
5. Validate telemetry event identity, kind, numeric bounds, and capture names
   before state mutation.
6. Add HTTPS/TLS 1.3 and optional HTTP client certificates, fail-safe bind
   defaults, origin/WebSocket checks, explicit methods, bounded request metadata,
   timeouts, and rate limits.
7. Keep secret material outside `graphx.yaml`; support mounted secret files and
   reject ambiguous or weak secret configuration.
8. Harden portable containers while keeping privileged OVS/macvlan/ipvlan
   laboratories explicit and separate.
9. Add unit, integration, negative, cancellation, compatibility, API, browser,
   Compose, sanitizer, and dependency checks proportional to the new boundary.

### Preserved invariants and compatibility constraints

- Configuration version remains 1. The optional TCP `tls` object is additive;
  existing files and plaintext behavior remain valid.
- The four-byte big-endian frame prefix and v1/v2 envelope bytes are unchanged.
- Timeout, cancellation, end-of-stream, malformed input, authentication failure,
  and transport failure remain distinct outcomes.
- TCP send deadlines, retry/reconnect semantics, complete-frame retry, and
  at-least-once caveats remain unchanged after a secure connection is established.
- TLS handshake cancellation is bounded and honors `Transport::close`.
- Secret values and message payloads are not logged by security diagnostics.
- Core graph/transport concepts remain independent of Docker, the browser,
  identity providers, certificate authorities, and telemetry vendors.
- Unsigned telemetry is retained only as a compatible loopback educational mode;
  it cannot enable HTTP runtime controls.

### Failure cases and explicit limits

| Boundary | Failure/limit |
|---|---|
| Native/HTTP bearer and HMAC secrets | inline and `_FILE` forms are mutually exclusive; 32-byte minimum; 4,096-byte maximum |
| TCP TLS | TLS 1.3 minimum; certificate/key required together; CA required for required client certificates; chain/name mismatch fails handshake |
| TLS cancellation | handshake polls cancellation every 25 ms within the configured connect timeout |
| UDP | 16 KiB maximum datagram; 30-second clock skew; 32-hex nonce; 64-hex signature; 4,096-entry replay cache |
| Telemetry identity | node/edge must exist in configured topology; event kinds, capture name/direction, identities, and numeric ranges are bounded |
| HTTP | 16 KiB headers; 64 headers; 2,048-character request target; 10-second request/header timeouts; 5-second keep-alive; 100 requests/socket |
| WebSocket | exact configured path/origin; authenticated subprotocol when required; 4 KiB inbound payload |
| Rate state | 120 general requests/minute/IP; 10 controls/minute/IP; separate 2,048-entry hard caps with expiry and oldest-entry eviction |
| Controls | POST only, no body, bearer required; pause/resume additionally require live HMAC-authenticated runtime endpoints |
| Plain HTTP | loopback only unless `GRAPHX_ALLOW_INSECURE_REMOTE=true` explicitly acknowledges the deployment boundary |
| Containers | read-only root, `/tmp` tmpfs, native-writable/collector-read-only capture volume, 128 PIDs, all capabilities dropped |

### Measurable acceptance criteria

- Ephemeral-certificate mutual-TLS GraphX round trip passes on C++20 and C++23.
- Peer-name mismatch fails and a stalled TLS handshake cancels within one second.
- Invalid/missing TLS configuration produces aggregated configuration diagnostics.
- HMAC tests reject tampering, replay, stale timestamps, unknown identities, and
  out-of-range values.
- The real three-process runtime accepts authenticated pause/resume and traffic
  demonstrably stops/restarts.
- HTTPS serves health, observation endpoints reject missing credentials, valid
  credentials succeed, wrong methods fail, security headers are present, and
  unsafe remote plaintext startup fails.
- Complete portable, sanitizer, web, Compose model/image, and dependency checks pass.

## 3. Requirements implemented

### Native TCP security

`TcpTlsOptions` is projected from the validated `transport.tcp.<edge>.tls`
configuration by `TransportFactory`. OpenSSL 3 contexts select client/server
roles, require TLS 1.3 or newer, disable compression, load certificate/key and
CA material, set SNI and peer-name verification for outbound connections, and
optionally require a verified client certificate on listeners.

TLS sessions remain owned by `TcpTransport` and are invalidated with the socket.
Encrypted reads account for bytes already buffered inside OpenSSL rather than
waiting incorrectly for new socket readability. Handshakes use short bounded
polls so a concurrent close cancels on macOS/Linux even when closing a descriptor
does not wake a long poll immediately. Reconnect creates and authenticates a new
TLS session before reporting the connection established.

### Telemetry and control authentication

`GRAPHX_TELEMETRY_SHARED_SECRET` and its `_FILE` form are consumed by runtime
nodes and the collector. Nodes wrap their existing JSON payload in a documented
HMAC-SHA256 envelope. Commands and acknowledgements use the same construction.
The collector authenticates before validation and before updating node state or
`controlEndpoints`; a spoofed unsigned datagram cannot redirect control in
authenticated mode.

The verifier should note the deliberate compatibility boundary: a runtime sink
constructed without a shared secret continues to accept unsigned commands from
its connected UDP peer for legacy unit/API compatibility, but the Phase 5 HTTP
collector refuses to enable controls without the HMAC secret. Production
deployment guidance requires the secret on every participant.

### HTTP, WebSocket, and browser boundary

The service has independent observation and control bearer credentials with
constant-time comparisons. Health is the sole public API endpoint. Topology,
capture metadata/files, metrics, and WebSocket snapshots require observation
authentication when configured. Reset, pause, and resume all require the control
credential; state-changing origins are same-origin or explicitly allowlisted.

HTTPS loads external certificate/key paths, fixes TLS 1.3 as the minimum, and
enables client-certificate verification when a client CA is provided. Request
targets are parsed against a fixed base rather than a trusted Host header.
Methods, body absence, sizes, timeouts, rates, origins, WebSocket path/protocol,
static roots, and capture names are validated before use. Security headers and
no-store API caching are applied consistently.

The browser stores the optional observation token only in session storage,
passes it to fetch and the WebSocket subprotocol, and performs authenticated
capture downloads through fetch/blob rather than an unauthenticated link. The
control token remains memory-only and is now used for reset as well as
pause/resume.

### Container and CI hardening

All standard Compose services use the existing non-root identities and now drop
all capabilities, disallow privilege escalation, use read-only roots and `/tmp`
tmpfs, cap processes at 128, and use an init process. Telemetry is published only
on host loopback. The demo's non-loopback container-internal HTTP/UDP binds are
explicitly declared; the private service remains plaintext for compatibility,
with TLS deployment paths documented.

The capture directory is owned by shared capture GID 65532 with mode `0770`, not
world-writable; telemetry joins that group and mounts the Compose volume read-only.
Native build/runtime images and every relevant CI job install OpenSSL 3
development/runtime packages. No healthcheck was added because health/readiness
policy is Phase 6.

## 4. Architecture and compatibility decisions

ADR 0006 records the layered boundary. TLS is per TCP edge because encryption
policy belongs to the transport endpoint, while key material remains externally
mounted. Observation and mutation bearer credentials are separate because read
access does not imply runtime-change permission. UDP uses signed messages rather
than a bearer field because integrity, freshness, and replay resistance are
required for a connectionless channel.

No OAuth/OIDC, user/role database, control audit ledger, or orchestrator-specific
secret object was added. Those choices would prematurely fix Phase 8 policy and
deployment technology. Likewise, telemetry remains best-effort/in-memory pending
Phase 7.

The accepted Phase 4 custom-build-directory finding was also remediated:
`GRAPHX_BUILD_DIR` now isolates both the C++23 and derived C++20 acceptance
directories, with an explicit `GRAPHX_CXX20_BUILD_DIR` override.

## 5. Files and major components changed

| Area | Files | Purpose |
|---|---|---|
| TLS model/API | `include/graphx/config.hpp`, `include/graphx/tcp_transport.hpp`, `src/config.cpp`, `src/transport_factory.cpp` | Additive validated TLS configuration and factory projection |
| TLS runtime | `src/tcp_transport.cpp`, `CMakeLists.txt` | OpenSSL 3 TLS 1.3/mTLS, verification, encrypted I/O, reconnect/close ownership |
| Native telemetry auth | `include/graphx/observability.hpp`, `src/observability.cpp`, `apps/common.hpp` | Secret loading, HMAC event/command/ack envelope, replay/freshness checks |
| Telemetry service | `apps/telemetry/server.mjs`, `apps/telemetry/security.mjs` | HTTPS/mTLS, bearer auth, HMAC verification, event/API/WebSocket validation, limits |
| Browser | `web/src/App.jsx`, `web/src/useTelemetry.js`, `web/src/components/EdgeInspector.jsx` | Observation credential, authenticated WebSocket/API/capture, reset authentication |
| Config schema | `config/schema/graphx.schema.json` | Strict optional TCP TLS object |
| Tests | `tests/tls_smoke.cpp`, `scripts/test-tls.sh`, `apps/telemetry/security.test.mjs`, `tests/test_config.cpp`, `scripts/test-features.sh` | TLS positive/negative/cancellation, HMAC/replay/input negatives, HTTPS/API/real-control acceptance |
| Containers/CI | `Dockerfile`, `docker/telemetry.Dockerfile`, `compose.yaml`, `.github/workflows/ci.yml` | OpenSSL dependency, least privilege, loopback publication, build matrix prerequisites |
| Documentation | `README.md`, `docs/security.md`, `docs/observability.md`, `docs/test-procedure.md`, ADR 0006 | Behavior, operations, limits, rationale, later-phase boundaries |

`prompt/implement.md` and `prompt/verifier.md` were already modified by the user
to select Phase 5 and are preserved as user changes.

## 6. Tests and checks run

All commands ran from `/Users/rklinkhammer/workspace/graphx-docker` on macOS
arm64 with AppleClang 21, OpenSSL 3.6.3, Node 26.8.1, npm 11.19, and Docker
Desktop. No existing container was stopped, removed, or recreated.

| Check | Result |
|---|---|
| Clean Phase 5 C++23 Debug configure/build | PASS; OpenSSL 3.6.3 found |
| Complete focused CTest after TLS implementation | PASS; 12/12 |
| TLS CTest | PASS; mTLS and reconnect round trips; untrusted CA, missing client certificate and name mismatch rejection; stalled-handshake cancellation |
| `npm test --prefix apps/telemetry` | PASS after remediation; 8/8 security and live-boundary tests |
| `npm test --prefix web` | PASS after remediation; 3/3 browser credential/control helper tests |
| `GRAPHX_BUILD_DIR="$PWD/build/phase5-remediation-portable" scripts/test-features.sh portable` | PASS; C++23 12/12, C++20 12/12, all configs/plans, process pipelines, SIGTERM, HMAC runtime controls, HTTPS/API security, captures/extcap, browser tests and web build |
| Custom C++20 build isolation | PASS; used `build/phase5-remediation-portable-cxx20` |
| ASan/UBSan preset and coverage audit | PASS; 13/13, including TLS and all-owned-source instrumentation |
| Web production build | PASS; 1,751 modules; existing approximately 1.847 MB chunk warning |
| Telemetry production dependency audit | PASS; 0 vulnerabilities |
| Web production dependency audit | PASS; 0 vulnerabilities |
| Compose model validation | PASS |
| Native and telemetry Compose image builds | PASS; GNU C++/OpenSSL 3 native build and Node telemetry image completed |
| `scripts/test-container-hardening.sh` | PASS; both fresh-volume initialization orders permit native writes and collector read-only access |
| Isolated four-service Compose capture deployment | PASS; three PCAPNG files discovered; collector mount is read-only; root filesystem/PID hardening confirmed; isolated project removed |
| cppcheck warning/portability analysis | PASS over production, apps, TLS/config tests |
| Local clang-format 23 dry-run on all Phase 5 C++ files | PASS |
| `git diff --check` | PASS |

The portable suite's HTTPS check generated an ephemeral self-signed certificate,
proved observation 401/200 behavior, method 405 behavior and security headers,
and required unsafe non-loopback plaintext startup to fail. Its real TCP runtime
test proved that authenticated pause stops the source counter and authenticated
resume restarts it.

### Checks not represented as successful local evidence

- The repository's exact formatter gate requires clang-format 18; this host has
  clang-format 23. Phase 5 files pass the available formatter, but the exact
  pinned gate remains for CI/verifier execution.
- clang-tidy is not installed on this host. cppcheck passed and the existing CI
  installs/runs clang-tidy 18; no local clang-tidy success is claimed.
- The modified GitHub workflow has not run remotely because changes are
  uncommitted. Only its local commands/configuration were exercised.
- Privileged native-Linux macvlan/ipvlan/OVS/netns/nftables/netem tests were not
  run on this macOS host. Phase 5 does not modify those infrastructure paths;
  all configuration and dry-run plans passed.
- The isolated Compose runtime used Docker Desktop's bridge network. Native-Linux
  capability and network-driver behavior remains covered by the separate
  privileged-tier restriction above.

## 7. Known limitations and verifier risks

1. **Authentication is not authorization:** bearer credentials are shared
   capabilities. There are no user identities, roles, per-action policy,
   revocation API, or durable audit record. These are Phase 8.
2. **Credential rotation:** file-mounted secrets are read at startup. Rotation
   requires atomic file replacement plus process restart.
3. **HMAC canonicalization:** interoperability signs the exact documented
   compact JSON payload serialization. Alternative emitters must reproduce that
   serialization or introduce a versioned canonical format.
4. **Clock dependence:** authenticated UDP peers must remain within 30 seconds.
5. **Replay cache scope:** replay state is in-memory/per process and intentionally
   bounded. Restart clears it; signed messages still expire by timestamp.
6. **Rate limit scope:** limits are per collector process and source IP, not a
   distributed edge/WAF control.
7. **Optional TLS:** version-1 compatibility means TCP remains plaintext unless
   the edge enables TLS. Deployment review must not assume global encryption.
8. **HTTPS deployment projection:** Compose demonstrates least privilege and
   host-loopback exposure but does not create production certificates/secrets.
9. **Unsigned compatibility mode:** direct legacy `UdpJsonTraceSink` tests can
   accept commands from the connected peer when no HMAC secret exists. The HTTP
   service correctly disables control in that mode; verifier should confirm this
   distinction remains explicit.
10. **Remote/host matrix:** Linux/macOS CI definitions were updated for OpenSSL,
    but the uncommitted workflow has not executed remotely.

Reverification should repeat the exact clang-format/clang-tidy 18 gates, Linux TLS
cancellation/reconnect, authenticated WebSocket accept/reject, replay/rate-cache
bounds, secret-file permissions, and confirmation that no secret appears in logs
or snapshots. The macOS run now covers untrusted CA, missing client certificate,
secret-file newline handling, malformed upgrade survival, and isolated Compose
least privilege.

## 8. Acceptance-criteria checklist

- [x] OpenSSL 3 is a justified native dependency and is present in CI/images.
- [x] TCP TLS 1.3 is optional, additive, and preserves framing/wire bytes.
- [x] Certificate chain and endpoint name are verified by default.
- [x] Mutual TLS is supported and positively exercised.
- [x] Peer-name mismatch and TLS handshake cancellation are negative regressions.
- [x] TLS configuration is strict, bounded, and covered by diagnostics tests.
- [x] Secrets remain outside graph configuration and support mutually exclusive `_FILE` forms.
- [x] Observation and control bearer credentials are separate and constant-time checked.
- [x] Reset, pause, and resume all require the control bearer.
- [x] Runtime control requires HMAC-authenticated telemetry endpoints.
- [x] UDP telemetry/control has integrity, freshness, replay, size, identity, and range checks.
- [x] HTTPS/TLS 1.3 and optional HTTP mutual TLS are implemented.
- [x] HTTP methods, bodies, targets, headers, timeouts, rates, origins, static paths, and capture paths are bounded.
- [x] WebSocket path, origin, credential, and payload are bounded.
- [x] Browser observation/API/WebSocket/capture authentication is implemented.
- [x] Plain telemetry defaults to loopback and unsafe remote plaintext requires explicit acknowledgement.
- [x] Portable Compose services are least-privilege and telemetry is host-loopback published.
- [x] C++20/C++23, sanitizer, API, real runtime control, web, dependency, Compose model, and image checks pass.
- [x] Documentation and ADR match implemented behavior and distinguish Phase 6/7/8 work.
- [x] No wire-format/config-version change or later-phase subsystem is hidden in Phase 5.

## 9. Recommended next work package

Proceed to **Phase 6: OpenTelemetry integration, health checks, SLOs, and
operational dashboards** only after independent Phase 5 verification accepts
this handoff.

Phase 6 should preserve TLS/authentication defaults, never export credentials or
payloads as telemetry, distinguish liveness from readiness, keep exporter queues
bounded and non-blocking, define measurable SLO semantics before dashboards,
secure OTLP transport explicitly, and avoid implementing Phase 7 persistence or
Phase 8 authorization policy prematurely.
