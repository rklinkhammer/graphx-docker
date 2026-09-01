# Phase 3 independent verification report — final remediation rerun

Date: 2026-08-31  
Repository: `/Users/rklinkhammer/workspace/graphx-docker`  
Work package: Protocol specification, compatibility rules, and message/trace identities

## 1. Verdict

**ACCEPTED**

All Phase 3 acceptance criteria are met. The prior blocking OTLP span-identity
collision is fixed and is covered by a real parent/child process regression test.
The prior formatting/handoff issue is also resolved for every C++ file in the
Phase 3 diff. No P0, P1, P2, or P3 finding remains from this verification.

## 2. Executive summary

Phase 3 provides a normative, transport-neutral GraphX envelope protocol with
explicit v1/v2 compatibility behavior. New messages use v2 with canonical
128-bit message and trace identities; readers retain exact v1 compatibility;
derived-message lineage is explicit; encoding is deterministic; and writers and
readers enforce documented limits before accepting data.

The implementation preserves GraphX's deployment-neutral architecture. Envelope
semantics remain independent of Docker, Compose, GUI code, and telemetry vendors.
The version change is explicit, migration order and rollback constraints are
documented, and no silent v2-to-v1 downgrade is available.

Independent verification included three fresh build directories, the complete
C++23 and C++20 CTest suites, ASan/UBSan, the portable feature suite, targeted
boundary and fork probes, static analysis, formatting of the actual changed C++
surface, dependency audits, web production build, Compose validation and image
builds, and a live four-service runtime smoke test. All required checks passed.

The previous P2 defect used process-inherited identity state to derive OTLP span
IDs, allowing a prefork parent and child to emit the same span ID. Span IDs are
now generated independently from operating-system entropy, must be non-zero, and
are rendered as 16 hexadecimal digits. Both the repository regression and a
verifier-owned executable reproduced the real parent/child export path and
observed distinct span IDs with the same canonical GraphX trace ID.

## 3. Acceptance-criteria matrix

| Requirement | Status | Implementation evidence | Independent validation evidence |
|---|---|---|---|
| Normative byte-level protocol specification | PASS | `docs/protocol.md` defines framing, scalar encoding, v1/v2 layouts, limits, identity semantics, and rejection rules | Documentation compared with serializer/parser behavior and golden fixtures |
| Valid v1 compatibility | PASS | v1 layout remains supported; decoded v1 lineage fields remain empty | 54-byte v1 fixture decoded and reserialized byte-for-byte; CTest passed |
| Explicit incompatible-version behavior | PASS | New roots emit v2; readers accept v1/v2 and reject unknown versions; no silent downgrade | Unknown-version, lossy-v1, and mixed-version cases passed |
| Canonical message, trace, and parent identities | PASS | `Envelope::make`, `Envelope::derive`, validation, serialization, and parsing implement the documented semantics | Round-trip, lineage, malformed-ID, and concurrent-generation tests passed |
| Stable logical identity through ordinary transform and retry | PASS | Transform copies the envelope; TCP serializes one logical frame before retry | Unit/integration tests passed; live sample and transformed events had the same message ID and trace ID at sequence 5 |
| Deterministic encoding and golden vectors | PASS | Attribute keys are sorted; v1/v2 fixtures are checked in | Exact 54-byte v1 and 92-byte v2 vectors passed byte-for-byte |
| Bounded writer and parser behavior | PASS | Complete envelope limit is 16,777,216 bytes; attribute count limit is 4,096; checked sizes precede reserve/acceptance | Exact maximum accepted; maximum plus one rejected; excessive attributes rejected |
| Strict malformed-input rejection | PASS | Parser rejects bad magic, unknown versions, malformed/zero identities, duplicate keys, truncation, and trailing bytes | Complete negative suite passed; verifier probe rejected every proper truncated v2 prefix |
| Failed in-process sends are atomic | PASS | `InProcessTransport::send` serializes and validates before publishing to the queue | Verifier probe observed an empty queue after invalid input and successful delivery of the next valid envelope |
| Timeout, cancellation, end-of-stream, and failure remain distinct | PASS | Existing typed receive results and lifecycle behavior are preserved | CTest lifecycle cases and portable shutdown tests passed |
| OTLP uses canonical trace IDs | PASS | Canonical envelope trace ID is emitted directly; legacy/noncanonical source values use the documented fallback | Captured OTLP JSON contained the expected 32-hex canonical trace ID |
| OTLP span IDs are valid and unique per exported span | PASS | `generate_span_id()` obtains a fresh non-zero 64-bit value from `getentropy`, with a best-effort standard-library fallback | Four same-process exports produced four valid distinct IDs; actual parent/child exporters produced distinct IDs after prefork state initialization; verifier probe reported `collide=0` |
| Telemetry failure remains best effort and bounded | PASS | OTLP queue has fixed capacity; failed network delivery is isolated to its worker; payload JSON is escaped | Existing telemetry contract tests, portable suite, and shutdown paths passed |
| Protocol/observability documentation matches behavior | PASS | `README.md`, `docs/protocol.md`, `docs/observability.md`, `docs/tcp-transport.md`, `docs/capture.md`, and ADR 0004 describe the implementation and deferred scope | Claims were compared with code, tests, live telemetry, and configuration output |
| Architecture remains deployment and vendor neutral | PASS | Core envelope and transport contracts have no Docker, GUI, or OTLP dependency | Include/dependency inspection found no placement or deployment coupling |
| No premature later-phase subsystem | PASS | Phase 3 adds protocol identity and the minimum existing-OTLP mapping only | No TLS/auth redesign, durable history, control plane, fuzz framework, dissector, or complete W3C propagation was introduced |
| Clean build, test, and deployment verification | PASS | Handoff lists the expected verification surface | Fresh builds, all suites, sanitizers, web build, audits, image builds, and direct runtime passed independently |
| Changed C++ code satisfies configured format | PASS | Tracked `.clang-format` applies to the Phase 3 diff | `git diff --name-only -z -- '*.cpp' '*.hpp' \| xargs -0 clang-format --dry-run --Werror` exited 0 |

The OTLP span shape was also compared with the OpenTelemetry Trace SDK identity
requirements: span IDs are non-zero 8-byte identifiers and each span receives a
new identifier. See the
[OpenTelemetry Trace SDK specification](https://opentelemetry.io/docs/specs/otel/trace/sdk/#sdk-span-creation).

## 4. Findings

No P0, P1, P2, or P3 findings were identified in the Phase 3 implementation.

Non-finding observations for Phase 4 planning:

- `cppcheck` reports two by-value performance suggestions in existing UDP and
  capture constructors. They are not correctness or acceptance issues.
- The production web build reports a large-bundle warning (approximately
  1.846 MB before gzip). It builds successfully and does not affect Phase 3.
- A repository-wide format run includes untouched legacy code that is not yet
  uniformly formatted. Every C++ file changed by Phase 3 passes the configured
  formatter; a repository-wide pinned policy belongs to Phase 4.

## 5. Tests and checks run

All commands were run from `/Users/rklinkhammer/workspace/graphx-docker` unless
otherwise stated.

| Check | Exact result |
|---|---|
| Fresh C++23 Debug configure/build in `build/verify-phase3-final` | PASS; AppleClang 21, Ninja build completed |
| `ctest --test-dir build/verify-phase3-final --output-on-failure` | PASS; 8/8 tests, 0 failures, 2.11 s |
| Fresh supported C++20 build in `build/verify-phase3-final-cxx20` | PASS |
| C++20 complete CTest | PASS; 8/8 tests, 0 failures, 2.04 s |
| Fresh ASan/UBSan build in `build/verify-phase3-final-sanitize` | PASS |
| ASan/UBSan complete CTest with `ASAN_OPTIONS=detect_leaks=0` | PASS; 8/8 tests, 0 sanitizer findings, 6.74 s |
| `scripts/test-features.sh portable` | PASS; C++23/C++20 suites, all topology validation, finite TCP and shared-memory pipelines, coordinated SIGTERM, telemetry HTTP semantics, web build, and authenticated pause/resume |
| Verifier boundary executable | PASS; v1=54 bytes, v2=92 bytes, all truncated prefixes rejected, exact 16 MiB accepted, maximum+1 rejected, lineage retained, failed-send queue empty, next valid send delivered |
| Verifier real-fork OTLP executable | PASS; two valid distinct span IDs, `collide=0` |
| Repository same-process and fork OTLP regressions | PASS as part of `graphx-tests` in all three fresh configurations |
| `node --check apps/telemetry/server.mjs` | PASS |
| Telemetry npm production audit | PASS; 0 vulnerabilities |
| Web npm production audit | PASS; 0 vulnerabilities |
| Web production build | PASS; 1,750 modules transformed; large-chunk warning only |
| `cppcheck` on envelope, in-process, observability, and capture sources | PASS/exit 0; two non-blocking by-value performance suggestions |
| Changed-file `clang-format --dry-run --Werror` | PASS/exit 0 |
| `git diff --check` | PASS |
| `docker compose -f compose.yaml config --quiet` | PASS |
| `docker compose -f compose.yaml build` | PASS; demo and telemetry images built |
| Direct `docker compose up -d --no-build` plus `scripts/demo.sh verify` | PASS; 4/4 services running, both edges connected, counters advanced 1 to 5, API and Prometheus live |
| Live `/api/topology` inspection | PASS; both sequence-5 receive events used wire version 2 and retained message ID `3ac0e63c3150731c94ea4866788ef2de` and trace ID `f17e64301d3b3c0a5b9777f8abd71827` |
| Container effective users | PASS; demo image `65532:65532`, telemetry image `node` |
| Runtime cleanup | PASS; Compose resources removed; `docker compose ps --all` empty |

The first local invocation of the boundary executable omitted its required source
directory argument and exited before exercising GraphX. It was immediately rerun
with the repository path and passed; this was a verifier command error, not a
product failure.

## 6. Unverified areas and why

- **LeakSanitizer:** Apple AddressSanitizer on this Darwin arm64 host does not
  provide the required leak-detection mode. ASan and UBSan otherwise passed.
- **Native Linux macvlan/ipvlan/OVS/netns:** Not run because the verifier host is
  macOS. Those features require a privileged native Linux environment. Phase 3
  did not alter the network-infrastructure implementation; all checked-in models,
  bridge Compose projection, and affected images were validated.
- **Coverage-guided fuzzing:** No fuzz target or CI job exists. This is explicitly
  Phase 4 scope. The verifier instead exercised all truncated v2 prefixes and the
  exact maximum-size boundary.
- **`clang-tidy`:** Not installed on this host and no repository job is configured.
  `cppcheck`, compiler diagnostics, sanitizers, and formatting were available and
  passed.
- **Full W3C trace context and backend interoperability:** Explicitly Phase 6.
  Phase 3 verifies only the existing OTLP JSON mapping of canonical GraphX trace
  IDs and independent span IDs.

These restrictions do not block Phase 3 acceptance because the unverified items
are either host-specific unchanged infrastructure or explicitly assigned to a
later work package.

## 7. Compatibility and security assessment

### Compatibility

- Existing v1 bytes remain readable and exactly reproducible.
- New roots emit v2; current readers accept v1 and v2; legacy v1 readers reject
  v2 explicitly.
- A received v1 envelope remains v1 during ordinary forwarding, avoiding an
  implicit upgrade in an intermediary.
- v2 cannot silently downgrade because v1 cannot carry message identity or
  lineage. Documentation correctly requires downstream-first rollout.
- Configuration remains version 1 and no topology schema changed in Phase 3.

### Security and resilience

- Untrusted wire input is bounded to 16 MiB and 4,096 attributes, rejects
  malformed identities and duplicate attributes, and requires exact consumption.
- Failed validation happens before an in-process message is published, preventing
  a ghost or unreadable queue entry.
- Message and trace IDs are documented as correlation values, not credentials,
  signatures, authorization tokens, or proof of origin.
- Span identity uses fork-safe operating-system entropy on supported macOS/Linux
  and rejects the reserved all-zero value.
- OTLP export is bounded and best effort; telemetry failure does not block graph
  processing.
- Metrics do not use message or trace identities as labels, avoiding a new
  unbounded-cardinality vector.
- Authentication, TLS, API hardening, origin/CSRF policy, and container security
  expansion remain visible Phase 5 work rather than being mislabeled as complete.

## 8. Required remediation before acceptance

None. The previous blocking span-ID collision and the changed-file formatting and
handoff discrepancy have been remediated and independently reverified.

## 9. Readiness for the next work package

The project is ready to proceed to **Phase 4: CI, sanitizers, fuzzing, static
analysis, and expanded transport tests**.

Phase 4 must preserve these Phase 3 invariants:

1. Exact v1 and v2 golden bytes and deterministic attribute ordering.
2. The 16,777,216-byte envelope/frame limit and 4,096-attribute limit, checked
   before allocation or publication.
3. Strict rejection of truncation, trailing data, duplicate attributes,
   malformed/zero identities, and unknown versions.
4. Stable logical message and trace identity across copying, serialization,
   transport, retry, and ordinary transformation.
5. Explicit derived-message parent lineage.
6. Distinct non-zero fork-safe OTLP span identity for every exported span.
7. Side-effect-free rejected in-process sends.
8. Bounded queues, best-effort telemetry, and distinct timeout, cancellation,
   end-of-stream, and failure outcomes.
9. Deployment/vendor neutrality and the documented downstream-first mixed-version
   rollout rule.

Phase 4 should prioritize a Linux/macOS CI matrix for C++20/C++23, repository-
pinned formatting and static-analysis jobs, a coverage-guided envelope/frame
decoder fuzz target, repeated lifecycle and shared-memory stress tests, exact
boundary properties, and mixed-version process integration tests. It should not
implement the Phase 5 security redesign or Phase 6 tracing expansion.

## Appendix A — Evidence map

| Area | Primary evidence |
|---|---|
| Wire format and compatibility | `docs/protocol.md`, `src/envelope.cpp`, `include/graphx/envelope.hpp`, `tests/fixtures/`, `tests/test_main.cpp` |
| Architecture decision | `docs/adr/0004-envelope-v2-identities-and-compatibility.md` |
| Transport atomicity and identity preservation | `src/in_process_transport.cpp`, `src/tcp_transport.cpp`, `apps/transform/main.cpp`, `tests/test_main.cpp` |
| OTLP mapping and fork-safe span identity | `src/observability.cpp`, `include/graphx/observability.hpp`, `tests/test_main.cpp`, `docs/observability.md` |
| Telemetry/API propagation | `apps/telemetry/server.mjs`, `web/src/components/EdgeInspector.jsx` |
| Capture metadata | `src/capture.cpp`, `include/graphx/capture.hpp`, `docs/capture.md` |
| Build and acceptance orchestration | `CMakeLists.txt`, `scripts/test-features.sh`, `docs/test-procedure.md` |
| Deployment smoke | `compose.yaml`, `docker/Dockerfile`, `docker/telemetry.Dockerfile`, `scripts/demo.sh` |

## Appendix B — Repository state

The repository was already intentionally dirty with the uncommitted Phase 3
implementation and documentation. Verification preserved those changes and did
not commit, push, publish, deploy, or modify external systems. The only verifier
deliverable changed is this report.
