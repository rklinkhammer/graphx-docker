# Phase 3 implementation handoff

Date: 2026-08-31  
Work package: Protocol specification, compatibility rules, and message/trace identities

## 1. Outcome summary

Phase 3 is implemented. GraphX now has a normative, transport-neutral envelope
protocol specification; byte-stable version-1 compatibility; a bounded version-2
wire format with canonical message, trace, and parent-message identities; explicit
root/derived-message semantics; mixed-version rollout rules; adversarial decoding
tests; and exact golden vectors. New messages emit v2. Current readers accept v1
and v2 and reject unknown versions or malformed input.

The first independent verification returned **CHANGES REQUIRED** with two P2
findings and one documentation P3. This remediation validates and serializes an
in-process envelope before queue publication, so a rejected envelope cannot
consume capacity or become visible. It also assigns every exported OTLP
operation a fresh, valid non-zero span ID instead of deriving span identity from
edge and sequence. The README now matches the authoritative ten-phase roadmap.
That implementation was then rerun through independent verification.

The remediation rerun then found one deeper P2: truncating the first half of the
GraphX identity generator allowed a prefork parent and child to export the same
span ID. Span IDs now come directly from operating-system entropy and have an
actual parent/child exporter regression after inherited identity state is
initialized. The rerun's P3 was also addressed: all changed C++ files now satisfy
the tracked `.clang-format`, and this handoff no longer claims the configuration
is absent. `phase_3_verification.md` remains the historical CHANGES REQUIRED
report until another independent verification replaces it.

Identity now reaches UDP telemetry, OTLP attributes, PCAPNG comments, the telemetry
collector, and the browser inspector. The TCP retry path constructs one complete
frame before retrying, so the logical message identity is stable. The demo
transform mutates the received envelope and therefore preserves message and trace
identity. Configuration version 1 is intentionally unchanged because configuration
and envelope wire versions are independent contracts.

The final remediation state passes fresh native C++23, C++20, sanitizer,
portable feature, web, Compose validation, container-build, and direct Compose
runtime checks. An earlier invocation of the all-in-one Docker wrapper hung in
the pre-Docker shared-memory example after the sink had received all eight
messages. That historical, nondeterministic Phase 2 lifecycle risk remains
recorded for Phase 4 stress work; the final portable suite and direct deployment
both completed cleanly.

## 2. Requirements implemented

| Requirement | Implementation evidence | Test/validation evidence | Status |
|---|---|---|---|
| Exact, documented envelope contract | `docs/protocol.md`; `src/envelope.cpp` | v1/v2 golden-vector tests | Complete |
| Preserve v1 compatibility | Dual decoder; v1 exact reserialization; legacy aggregate default | `envelope protocol golden vectors` | Complete |
| Version incompatible change | `GXE` v2 layout and explicit version rejection | Unknown-v3 negative tests | Complete |
| Stable logical message identity | `message_id`, `Envelope::make`, copy/transport semantics | round trip, transform-copy, concurrent sample, TCP/shared-memory pipelines | Complete |
| Trace and causal lineage | canonical `trace_id`, optional `parent_message_id`, `Envelope::derive` | identity-semantics test | Complete |
| Stable retry identity | TCP serializes once before complete-frame retry | code inspection plus existing reconnect tests | Complete |
| Bounded untrusted parsing | 16 MiB envelope/frame limit, 4,096 attributes, checked size arithmetic | oversized, truncation, excessive-count tests; ASan/UBSan | Complete |
| Strict malformed-input handling | rejects bad magic/version, truncation, trailing data, duplicate keys, invalid/zero identities | adversarial protocol test | Complete |
| Deterministic encoding | sorted attributes and exact scalar rules | byte-for-byte golden reserialization | Complete |
| Mixed-version deployment rules | downstream-first matrix and rollback boundary | documentation comparison against codec behavior | Complete |
| Observability/capture propagation | capture metadata, UDP JSON, OTLP trace mapping/attributes | PCAPNG, UDP, OTLP, portable telemetry contract | Complete |
| Failed-send atomicity | in-process serialization/validation occurs before queue publication | invalid ID, forbidden v1 lineage, oversized-envelope regression followed by valid delivery | Complete |
| Valid OTLP span identity | fresh fork-safe non-zero 64-bit OS-random ID per exported operation; canonical trace mapping retained | cross-exporter send/receive, repeated-processing, and actual prefork parent/child regressions | Complete |
| GUI correlation | exact `messageId` correlation with documented v1 fallback | production web build and portable API contract | Complete |
| Consequential decision recorded | ADR 0004 | documentation review | Complete |
| Later phases not implemented early | no TLS/auth redesign, fuzz harness, full OTel context, durable history, or dissector work | scope review | Complete |

## 3. Architecture and compatibility decisions

1. Envelope v2 adds three fixed 16-byte identity fields while retaining the
   existing magic, big-endian scalars, deterministic attributes, and opaque byte
   strings. An all-zero parent means absent; required identities cannot be zero.
2. API identities are exactly 32 lowercase hexadecimal characters. They are
   correlation values, never credentials, authorization tokens, or signatures.
3. `message_id` identifies one logical message and is the v2 duplicate-suppression
   key. Sequence expresses ordering and trace ID groups causal work; neither is a
   unique-message key.
4. `Envelope::make` creates a v2 root with new message and trace IDs.
   `Envelope::derive` creates a new logical message, preserves a canonical parent
   trace, and records the parent message ID. Ordinary mutation/copy preserves all
   identities.
5. Current readers decode v1 and v2. A decoded v1 envelope remains v1 when
   forwarded. Serializing v1 with v2-only lineage fails instead of losing data.
   V1 readers reject v2, so rolling upgrades are consumers/downstream first.
6. The identity generator uses per-process entropy, monotonic time, and an atomic
   counter. It is concurrent-safe and collision-resistant for correlation, but
   deliberately makes no cryptographic-authentication claim.
7. Canonical GraphX trace IDs map directly to OTLP trace IDs. W3C trace context,
   span-parent propagation, sampling, and exporter conformance remain Phase 6.
   Each current exported operation has a fresh OS-random 64-bit span ID
   independent of edge, sequence, message identity, and inherited prefork state.
8. The transport, configuration, Docker, GUI, and telemetry boundaries remain
   separate. No new external dependency was introduced.
9. In-process send validates the same canonical serialization used for telemetry
   before mutating its bounded queue. Validation failure is side-effect-free;
   backpressure behavior still begins only after validation succeeds.

Alternatives rejected in ADR 0004 include reusing sequence or trace as message
identity, silently changing v1, textual UUID bytes, silent downgrade, and pulling
the later OpenTelemetry propagation phase into this work package.

## 4. Files and major components changed

- Protocol/API: `include/graphx/envelope.hpp`, `src/envelope.cpp`.
- Capture/telemetry: `include/graphx/observability.hpp`, `src/capture.cpp`,
  `src/observability.cpp`, `apps/common.hpp`, `apps/telemetry/server.mjs`.
- GUI: `web/src/components/EdgeInspector.jsx`.
- Tests/fixtures: `tests/test_main.cpp`, `tests/fixtures/envelope-v1.hex`,
  `tests/fixtures/envelope-v2.hex`, `CMakeLists.txt`, `scripts/test-features.sh`.
- Normative and operational documentation: `docs/protocol.md`,
  `docs/adr/0004-envelope-v2-identities-and-compatibility.md`, `README.md`,
  `docs/tcp-transport.md`, `docs/observability.md`, `docs/capture.md`, and
  `docs/test-procedure.md`.
- Verification routing: `prompt/verifier.md` now names this handoff.

No configuration schema or checked-in topology changed.

Verification remediation changed only `src/in_process_transport.cpp`,
`src/observability.cpp`, the associated regression tests, formatting of changed
C++ files, and documentation. It did not change v1/v2 bytes, public APIs,
dependencies, topology, or deployment configuration.

## 5. Tests and checks run

| Check | Exact result |
|---|---|
| Clean configure/build: `build/phase3-fork-remediation`, Debug, C++23, Ninja | PASS; fresh AppleClang 21 configure and 57 build steps completed |
| `ctest --test-dir build/phase3-fork-remediation --output-on-failure` | PASS; 8/8 tests, 0 failures, 2.20 seconds, including queue-atomicity and prefork OTLP regressions |
| Fresh supported C++20 build and CTest in `build/phase3-fork-remediation-cxx20` | PASS; 57 build steps; 8/8 tests, 0 failures, 2.06 seconds |
| `scripts/test-features.sh portable` after final test changes | PASS; both standards, every topology, dry-run infrastructure, TCP/shared-memory pipelines, SIGTERM, PCAPNG/extcap, telemetry/API/control, and web build |
| Fresh ASan + UBSan build in `build/phase3-fork-remediation-sanitize` | PASS; 57 build steps |
| Sanitized CTest | PASS; 8/8 in 4.68 seconds with `ASAN_OPTIONS=detect_leaks=0`; Apple ASan does not support leak detection |
| JavaScript parse check | PASS: `node --check apps/telemetry/server.mjs` |
| Web production build | PASS; Vite built 1,750 modules. Existing large-chunk warning remains (about 1.846 MB minified main bundle) |
| Telemetry/API identity contract | PASS in UDP/OTLP unit tests and portable feature suite; verifier-owned actual-exporter prefork probe changed from `collide=1` before remediation to `collide=0` after remediation |
| Compose model | PASS: `docker compose -f compose.yaml config --quiet` |
| Affected container builds | PASS: `graphx-demo:latest` and `graphx-telemetry:latest` |
| Direct Compose runtime and smoke verification | PASS after remediation production changes: four services running, both TCP edges connected, both counters advanced 1 to 5, API/Prometheus live; resources then removed cleanly |
| `cppcheck` on envelope, in-process, observability, and capture production units | Exit 0; two non-blocking existing by-value performance notices in UDP/capture constructors |
| `git diff --check` | PASS |
| Clang formatting dry run | PASS over all changed C++ files using the tracked `.clang-format` |
| `clang-tidy` | Unavailable and not configured |
| Prior all-in-one Docker wrapper attempt | Historical NOT PASS from the original handoff: shared-memory close stalled nondeterministically. The final portable run and direct Compose smoke both passed; repetition remains Phase 4 work |
| Native macvlan/ipvlan/OVS/netns tier | Not run: macOS is not a driver-accurate native Linux environment and Phase 3 did not change infrastructure behavior |

No fuzz job exists; adding fuzzing and formal CI jobs is explicitly Phase 4.

## 6. Known limitations

- V1 messages have no protocol-level message identity. Telemetry/capture uses the
  documented trace-plus-sequence fallback, which cannot provide v2 deduplication
  guarantees.
- The generator does not provide cryptographic unpredictability or persistent
  allocation across restored process snapshots. IDs are not security material.
- The API struct remains mutable for educational simplicity; identity immutability
  is a semantic contract enforced at serialization boundaries and by runtime use,
  not by private setters.
- Full W3C/OpenTelemetry context, sampling, span-parent IDs, exporter retry
  conformance, and operational dashboards remain Phase 6.
- The existing PCAPNG/USER0 capture is an educational private-use representation;
  a registered link type/native dissector remains Phase 9.
- LeakSanitizer could not be exercised on this Apple platform. ASan and UBSan did
  run successfully with leak detection disabled.
- The pre-existing nondeterministic shared-memory close stall needs Phase 4
  stress/regression work even though the standalone portable suite passed.

## 7. Risks for independent verification

1. Independently reconstruct both golden layouts and verify the fixed v2 offsets,
   especially the 48 identity bytes and the first variable field at offset 68.
2. Exercise more arbitrary truncation/length combinations and maximum-boundary
   values; Phase 4 fuzzing should turn this into continuous coverage.
3. Verify a real mixed rollout in process pairs: new reader/old writer succeeds,
   old reader/new writer rejects cleanly, and a v1 intermediary does not silently
   receive v2 before it is upgraded.
4. Confirm TCP reconnect sends the same serialized v2 bytes in both attempts,
   including a failure after the peer may have accepted the first frame.
5. Examine identity generation under fork/process snapshot behavior and decide
   whether production requirements warrant an OS CSPRNG-backed UUID/trace-ID
   facility in a later security/release phase.
6. Reproduce the shared-memory close stall under repetition and sanitizers. It was
   outside the Phase 3 protocol change but can block the aggregate Docker wrapper.
7. Inspect JSON escaping and cardinality implications now that three identity
   fields reach telemetry. Current metrics do not use identities as labels.
8. Re-run the rejected-envelope regression and confirm invalid v2 identity,
   forbidden v1 lineage, and oversized serialization leave the in-process queue
   empty before a following valid message succeeds.
9. Independently collect send, receive, and repeated processing spans for one
   trace and confirm all 16-hex-digit span IDs are non-zero and distinct across
   exporter instances. Initialize GraphX identity state before `fork()` and
   repeat with actual parent/child exporters.

## 8. Acceptance-criteria checklist

- [x] Normative byte-level v1/v2 protocol specification exists.
- [x] Wire-format change is explicitly versioned and migration is documented.
- [x] New messages have canonical non-zero message and trace IDs.
- [x] Derived messages have explicit parent lineage.
- [x] Retry, mutation, serialization, and transport preserve logical identity.
- [x] Current decoder accepts v1/v2 and rejects unknown versions.
- [x] V1 round-trip is byte stable; lossy downgrade is rejected.
- [x] Size limits are enforced before writer allocation and reader parsing.
- [x] Truncated, oversized, duplicate, unknown, trailing, and invalid-identity
  cases have negative coverage.
- [x] Golden fixtures verify exact cross-version bytes.
- [x] Capture, telemetry, OTLP, collector, and GUI expose/correlate identities.
- [x] In-process validation failure cannot publish or consume queue capacity.
- [x] Every OTLP operation gets a distinct valid span ID while retaining the
  envelope trace ID.
- [x] README, protocol, transport, capture, observability, test procedure, and ADR
  agree with implementation.
- [x] Clean C++23/C++20 builds and complete CTest suites pass.
- [x] ASan/UBSan tests pass within platform capability.
- [x] Web and affected container builds pass; direct Compose runtime passes.
- [x] No new dependency, deployment coupling, or premature later-phase subsystem
  was introduced.
- [x] Skipped/failed checks and limitations are explicit.

## 9. Recommended next work package

Proceed to Phase 4 only after independent Phase 3 verification accepts this
handoff. Phase 4 is CI, sanitizers, fuzzing, static analysis, and expanded
transport tests. It should preserve the exact v1/v2 golden bytes, the 16 MiB and
4,096-attribute bounds, strict decoder behavior, stable retry identity,
side-effect-free failed sends, unique valid OTLP span IDs, downstream-first
compatibility policy, transport-neutral semantics, and distinct timeout,
cancellation, end-of-stream, and failure outcomes.

Phase 4 should prioritize a coverage-guided envelope/frame decoder fuzz target,
CI matrices for C++20/C++23 and Linux/macOS sanitizers, a repository-pinned format
and static-analysis policy, maximum-boundary property tests, mixed-version process
tests, repeated lifecycle/stress tests, and a regression for the observed
shared-memory close stall. It should not implement Phase 5 security or Phase 6
OpenTelemetry behavior early.
