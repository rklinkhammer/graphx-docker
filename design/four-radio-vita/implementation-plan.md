# Four-radio VITA implementation plan

## Scope and execution rules

This plan implements [the accepted VITA brief](../../docs/vita_system.md) under
[the repository decisions](../../docs/project-decisions.md). P1 is in progress:
the opt-in standalone radio and local wire/device tests are implemented. See
[P1 verification](p1-verification.md) for evidence and remaining work. P2–P6 are
planned. This plan does not authorize privileged execution.

The first deliverable is a real, independently tested radio. Do not make its
development depend on the final IQ processor, feature detector, recorder or OVS
extensions. Use test clients and receivers to exercise its public wire interfaces.
These are test harnesses, not alternative production launchers or manifests.

Accepted architecture and behavior remain fixed. Resolve ordinary engineering
choices in the relevant phase, recording rationale and measurable limits. Report
only genuinely new blocking conflicts before affected work; continue independent
work. A design artifact is required before its corresponding implementation, but
this plan does not introduce a separate mandatory user approval gate.

| Phase | Deliverable | Depends on |
|---|---|---|
| P1 | Standalone virtual radio, VITA control/data and independent acceptance | None |
| P2 | Four-stream IQ processor, power FFT protocol and frequency detector | P1 |
| P3 | Authored OVS jumbo paths and container mirror-recorder support | P1; P2 packet budget |
| P4 | Degraded startup and bounded application failure handling | P3 |
| P5 | Verified images and complete four-radio authored graph | P1–P4 |
| P6 | End-to-end acceptance, observation and user documentation | P5 |

Implement sequentially by default. P3 infrastructure design can proceed while P2
is developed, but must use its finalized FFT message budget before acceptance.

## P1 — Develop and test the radio as a standalone entity

**Outcome:** one production radio executable owns one virtual `SoapySDR::Device`,
accepts authenticated VITA commands and emits correct, paced VITA IQ bursts. A
test controller and independent receiver demonstrate this without the final system.

### Command-analysis work items

Apply [P1.1 command analysis](command-analysis.md) to the P1 exit criteria:

| Item | Required result | Current state |
|---|---|---|
| P1.1 | Inspect vrtgen examples, query/ack generation and supported mappings | Analysis complete |
| P1.2 | Explicit NO_ACTION status selectors, separate generated query acknowledgment, replies matching requested execution/status modes | Implemented; focused wire tests pass |
| P1.3 | Field-specific range/precision errors, late-start timing error, atomic rejected configuration | Implemented for supported settings; focused wire tests pass |
| P1.4 | Read-only VITA capability command/response over existing TCP/mTLS; verified field mapping, generated codec and independent vectors | Command/response architecture selected; encoding and runtime implementation remain open |
| P1.5 | Complete independent context/ack coverage, non-divisor timing, replay exhaustion, stalled-client and resource-bound tests | Remaining acceptance work |

P1.4 uses the [selected capability-query contract](radio-design.md#selected-capability-query-contract).
Streaming context remains applied-state information; it is not the capability
discovery channel. Reuse the existing authenticated control connection and device
owner. This architecture is decided and does not require another protocol choice.

P1.4 must distinguish supported limits from current values and statistical
attributes, and distinguish global bandwidth limits from constraints imposed by
the applied sample rate. Do not assume CIF7 precision means a supported increment.
If CIF7 is selected, correct and test its acknowledgment inheritance in the pinned
generator before using it; YAML additions alone do not establish a working codec.
Preserve the existing source pin, notices and reproducible generation inputs.

P1.5 proceeds independently of P1.4. No OVS deployment or privileged permission is
needed for these local tests. P1 cannot close while capability queries remain
unimplemented; do not replace that requirement with status or static documentation.

### Framework and codec progression

Own the controller/controllee framework and buffer policies in GraphX. Retain
vrtgen as the first packet codec behind a replaceable adapter. This progression
does not select a custom codec or claim comprehensive VITA 49.2 support.

| Step | Work and evidence | Phase boundary |
|---|---|---|
| 1. Define interfaces | Define GraphX-owned command, query, acknowledgment, context and IQ types using the [packet-format reference](../../docs/vita_packet_formats.md). Separate profile validation from byte encoding and device operations. Keep generated types behind the codec adapter. | P1 design and implementation |
| 2. Extract framework and buffers | Extract controller request/correlation/retry logic, controllee dispatch/replay/atomic-apply state machines and streaming buffer ownership. Define bounded pools/queues, overload behavior, deadlines and shutdown. Exercise the reusable controller with the standalone radio harness. | P1; P2 integrates that controller into the IQ application |
| 3. Integrate the initial codec | Put the pinned vrtgen implementation behind the interfaces, preserving source pins, notices, reproducible generation and narrowly tested corrections. Reuse existing TLS, resolved bindings and lifecycle facilities. | P1 |
| 4. Establish independent evidence | Add independent wire vectors, malformed-input/fuzz tests, sanitizer checks and state-machine failure tests. Measure allocations, copies, queue bounds and throughput under a declared load. Codec round trips alone are insufficient. | P1 local evidence; P3/P6 validate actual network paths/load |
| 5. Evaluate a custom codec | Compare a bounded, explicitly supported VITA subset against the same interfaces, vectors, safety checks and performance measurements. Record maintenance, interoperability and license implications before selecting replacement. | Optional follow-on evaluation; not a prerequisite for P1 completion |

Steps 1–4 are planned work; the current standalone radio is not evidence that this
framework extraction is complete. Record its implementation and tests in
`radio-design.md` and `p1-verification.md`. P1 exits with the owned framework and
verified vrtgen adapter; a custom-codec evaluation must not delay unrelated P1 work.

Use reusable buffers with explicit ownership first. Introduce zero-copy packet
views only where measurement justifies them and their lifetimes cannot outlast or
prematurely recycle the backing buffer. Keep control responsive under streaming
overload. Unsupported packet combinations must fail before field access/allocation.

Changing codecs does not resolve open capability-field semantics. Resolve those
through the common packet reference and authoritative evidence. Any eventual
replacement must retain the agreed wire profile, independent verification and
dependency/license inventory; do not silently substitute proprietary fields.

### Design and dependency integration

- Create `design/four-radio-vita/radio-design.md` with the device API, lifecycle,
  packet layouts, timing model, limits and test approach. Include exact control/CAM
  mappings, applied acknowledgments, timed-start encoding, idempotency and context
  cadence. Choose deterministic defaults rather than reopening accepted behavior.
- Pin and verify SoapySDR and `vrtgen`; maintain packet generation inputs and
  reproducible generation steps. Record licenses, generated-code notices, build
  requirements and supporting libraries in dependency/release inventories and SBOM
  inputs. Do not use the reviewed upstream revision as an unverified release pin.
- Separate VITA codec behavior, virtual device, radio orchestration and thin app
  entry point under the existing `include/graphx/`, `src/` and `apps/` conventions.
- Add the minimal radio catalog/configuration contract through the authoritative
  C++ loader and normalized bindings. Standalone test fixtures must exercise these
  contracts; no radio-only production configuration parser.

### Radio implementation

- Implement the accepted fixed-absolute-RF tone, receiver center frequency,
  sample rate, ideal bandwidth, gain/clipping and deterministic continuous phase.
  Apply the accepted defaults: 1 MSample/s, 800 kHz bandwidth, 0 dB gain and
  0.25 full-scale amplitude. Parameterize radio identity and tone frequency so the
  same executable supports all four radios.
- Generate signed 16-bit I/Q through the device streaming interface. Implement
  bounded reads, serialized configuration changes and capability/range validation.
- Generate stream IDs 1–4, zero class ID and required trailer using the selected
  packet definitions. Emit up to 1,024 pairs per packet and up to 1 MiB per burst;
  preserve valid short payloads and continuous sample time/phase across bursts.
- Implement mutually authenticated TCP control with bounded VITA framing. Provide
  configuration, applied status, capability query, scheduled start and stop. The
  device/control listener becomes ready before configuration; no IQ is emitted
  until configuration succeeds and the separately commanded start time arrives.
- Define the simulated epoch and host-deadline mapping. Pace continuous bursts at
  the sample rate; specify late-start and pacing-overrun behavior and bounded retry
  handling. Keep control responsive during streaming and after data-receiver loss.

### Standalone verification

Use unprivileged local TCP/TLS and UDP sockets with ephemeral ports and staged test
credentials. Run the actual radio executable under a bounded test harness, stopping
it explicitly after each case. Loopback reception proves codec/process behavior;
it does not prove an OVS jumbo path or absence of fragmentation on that path.

1. Configure and query the radio; prove configuration alone emits no IQ. Schedule
   a start, receive multiple complete bursts, stop streaming and query it again.
2. Independently decode received bytes and compare known sample values, tone offset,
   gain, passband rejection, clipping, phase continuity and timestamps. Use vectors
   or a decoder independent of the generated encoder, not only a codec round trip.
3. Verify full/partial packets, single/multiple-packet bursts, size accounting,
   context, required trailer, marker mapping and packet-count wrap.
4. Exercise fragmented/coalesced TCP messages, malformed sizes/types, unauthorized
   peers, unsupported settings, duplicate commands, negative acknowledgments and
   disconnect/reconnect without resetting a running stream.
5. Verify scheduled start, bounded jitter against the declared tolerance, pacing,
   interrupted shutdown and bounded operation without a receiver. Test the device's
   supported setting-change semantics without implementing automatic retuning.
6. Repeat standalone tests with radio IDs 1–4. Also run independent radio processes
   with distinct settings to detect shared state or cross-radio command effects.
7. Independently check query selector-only lengths, selected reply fields, CAM
   action/acknowledgment bits and correlation IDs. Request execution and status
   together and verify both replies; execution-only must not produce extra status.
   Check field-specific range/precision errors and late-start timestamp errors,
   verifying unchanged settings after rejection. Keep malformed-query tests separate
   from execute-packet payload validation.
8. Query capabilities before configuration, while stopped and while streaming.
   Verify response correlation, supported ranges and cross-field constraints against
   the device API using independent packet decoding. Prove no settings, phase,
   timestamps or streaming state change, and no dependency on UDP context reception.
   Exercise unauthorized queries, unsupported selectors and bounded response sizes.

**Exit evidence:** `p1-verification.md` records commands, environment, measured
results and failures. The production radio passes standalone wire/device tests;
the final IQ application and OVS are not prerequisites. Include a reproducible
developer invocation of the harness and its fixture. No container/network-path
acceptance is claimed from these tests.

## P2 — IQ processing, FFT messages and feature detection

**Outcome:** the standalone radio feeds the real processor; the processor emits
bounded power spectra consumed by the real feature detector.

- Write `processing-design.md` before coding the wire protocol. Select power
  precision/byte order, normalization, full complex-IQ frequency-axis ordering,
  metadata/envelope layout, supported FFT lengths, windows and overlap units/limits.
  Do not discard negative-frequency signal information.
- Calculate the complete serialized size, including bounded gap metadata and any
  GraphX envelope. One result must fit one jumbo UDP datagram without fragmentation.
  Derive supported bin widths from sample rate, FFT length and this size limit;
  reject unsupported combinations. Select and inventory an FFT dependency if needed.
- Implement four independently bound UDP receivers with expected source/stream
  validation and separate per-stream context, sequence, reorder and sample state.
- Implement the application controller using the P1 wire contract: bounded
  configuration window, common scheduled start for successful radios, independent
  connection handling, no reinitialization on ordinary reconnect.
  Reuse the P1 controller framework and codec interfaces rather than creating a
  second command encoder, correlation mechanism or retry state machine.
- Assemble FFT windows across consecutive bursts. Zero-pad missing sample positions
  after a bounded reorder deadline, retain quality/count metadata and avoid filling
  an unlimited outage with unlimited synthetic work. A short packet alone is not
  missing data. Do not let one failed stream stall healthy streams.
- Implement per-packet detected RF center frequency from the power spectrum, with
  explicit no-detection for zero/invalid spectra. Select a simple deterministic peak
  estimator and numerical tolerance. Use existing bounded Node console/log facilities
  for the initial display, carrying stream and FFT-time identity; do not silently
  discard results to make display throughput appear sufficient. Surface any output
  drops and retain bounded history.
- Add authoritative catalog/wire schemas and fixtures; keep FFT UDP distinct from
  the existing TCP `RawPowerResult`. No feedback edge or automatic tuning loop.

**Verification:** known-tone and packet-size boundary tests; configurable windows
and overlap; negative/positive tone offsets; FFTs crossing bursts; packet loss,
duplicates/reordering and partial IQ packets; invalid metadata; detector loss;
all-zero spectra; bounded prolonged outages. Exercise four standalone radios with
the real controller/processor/detector on local test sockets. This is functional
acceptance, not final OVS routing evidence.

**Exit evidence:** `p2-verification.md` demonstrates correct power/frequency results
and exact message-size accounting for every supported FFT size. Record nominal
output rate at supported overlap settings and bound the display/processing load.

## P3 — OVS jumbo paths and the recorder application

**Outcome:** the authoritative graph can express the required packet paths and
deliver mirrored frames to a separate bounded receive/discard application.

- Write `network-design.md`: authored/normalized MTU fields, endpoint propagation,
  recorder attachment/capability model, optional independent diagnostic capture,
  ownership checks and interruption/cleanup behavior.
- Extend the C++ loader, schemas, normalization, compiler artifacts and tests
  together. Configure and verify both veth ends and the mirror delivery path.
  Use 9,000-byte IP MTU where supported, validating actual VITA/FFT packet budgets
  and rejecting insufficient paths before data emission.
- Extend the common mirror lifecycle to deliver to a verified container namespace.
  Define the minimum capture permissions, restrict the passive endpoint, prevent
  network injection and retain interface/namespace identity in the ownership ledger.
  Do not grant Docker/OVS sockets or general host networking.
- Implement the separate recorder app/catalog type: bounded raw-frame receive and
  discard, packet/byte and available drop/error counters, readiness and logs.
  No archive, packet files, recorder-last ordering or lossless-drain requirement.
- Support independent GraphX diagnostic capture when explicitly enabled, without
  replacing the recorder path or its counters. Keep capture retention bounded.

**Verification:** configuration rejection and compiler fixtures first; identity,
permissions, malformed MTU and cleanup tests. With explicit authorization, run an
isolated native-Linux/Lima graph to verify jumbo unicast, complete mirrored frames,
duplicate-selection behavior, passive attachment, independent diagnostic capture,
recorder failure isolation and owned cleanup. Record the 16 MB/s nominal source
payload separately from measured traffic and observed best-effort reception.

**Exit evidence:** `p3-verification.md` separates portable contract tests from actual
privileged path tests. Unrun privileged checks remain pending, not passed.

## P4 — Degraded startup and application failure handling

**Outcome:** healthy application components start and continue when peers fail,
without weakening infrastructure identity/security checks or restarting containers.

- Write `lifecycle-design.md` with authored readiness/failure policy, release-barrier
  semantics, ready/degraded/unavailable status, bounded deadlines and cleanup rules.
  Preserve current transactional defaults for other graphs unless explicitly selected.
- Extend the common lifecycle to release the available application subset after
  bounded readiness attempts. Distinguish application unavailability from invalid
  configuration, authentication, release or infrastructure ownership failures.
- Attempt recorder readiness before release, but permit degraded startup without it.
  An unavailable radio cannot prevent configured radios from starting. A missing
  processor means radios cannot receive initial configuration/start; report that
  dependency honestly instead of claiming useful acquisition.
- Keep surviving processes running after runtime failure. A running radio continues
  after controller loss; detector/recorder failure cannot block upstream work.
  Surface stale/unavailable results, not fabricated progress.
- Enforce `restart: no`. Connection retries in surviving processes remain bounded
  and do not recreate containers. Use the existing explicit whole-graph restart for
  operator restoration in this phase; individual-container replacement/hot rejoin is
  not required. Document that deliberate whole-graph restart interrupts healthy nodes.

**Verification:** fail each application before readiness and after release; verify
surviving subset, barrier/status behavior, bounded memory/work and no respawn.
Exercise all-radios-unavailable and shared-network failure states without claiming
useful throughput. Verify invalid identities still fail closed and existing graphs'
transactional startup tests retain their previous behavior.

**Exit evidence:** `p4-verification.md` includes fault outcomes and owned-resource
inventories. State explicitly which failures permit continuation and which reject
an unsafe operation.

## P5 — Release packaging and complete authored example

**Outcome:** the standard GraphX CLI runs the complete seven-application graph:
four radios, IQ processor/controller, feature detector and recorder.

- Extend shared release/image roles with the verified SoapySDR, `vrtgen`, FFT and
  application artifacts. Include generation provenance, required licenses and SBOMs;
  verify ARM64 Linux support and the selected release artifacts rather than inventing
  image digests. Exercise the packaged radio with the P1 harness as a regression.
- Author `examples/four-radio-vita/graphx.yml`: four radio identities, four distinct
  data listener ports and four radio control ports, explicit source bindings, OVS
  edges, jumbo MTU, FFT connection, passive recorder and selected degraded policy.
- Stage mutually authenticated control credentials through the existing mechanism.
  Preserve separate management connectivity and default authenticated browser access.
- Add bounded verification scenario actions only through the existing scenario model.
  No source Compose overlay, private launcher or privileged web API.

**Verification:** loader/compiler golden and negative tests; release/image validation;
configuration/credential checks; actual CLI plan/up/status/logs/down on an authorized
Linux/Lima environment. Inspect all application paths for management-network bypass.

**Exit evidence:** `p5-verification.md` identifies exact release pins, target/engine,
runnable commands and the complete graph's observed state. Packaging success alone
does not establish full acceptance.

## P6 — Integrated acceptance, observation and documentation

**Outcome:** every requirement in the brief has implementation and verification
evidence, or is explicitly identified as blocked/deferred.

- Run sustained four-radio streaming at the accepted defaults, scheduled startup,
  FFT/detection and best-effort recorder observation. Demonstrate packet-size and
  timestamp correctness, no IP fragmentation, sample-rate pacing and bounded memory.
  Measure startup skew and frequency error against the limits chosen in P1/P2.
- Inject loss/reordering and application failures using owned scenario resources;
  verify padded FFT quality, continuing healthy streams, no automatic restart,
  responsive control and honest unavailable states. Confirm bounded shutdown without
  requiring final-packet capture. Preserve unrelated workloads and retained evidence.
- Verify browser topology, OVS paths, node logs, detected-frequency presentation,
  degraded status and optional diagnostic capture. Do not require readable TLS
  commands in a passive packet capture; use authorized codec/endpoint evidence.
- Update the example README, example matrix, quick start, user guide and architecture
  with actual CLI commands, supported FFT settings, no-restart behavior, operator
  whole-graph recovery, best-effort capture and current limits.
- Complete `verification.md` with **Requirement | Implementation | Verification |
  Status**, linking phase evidence and generated artifacts. Remove or qualify any
  future-command language only after the corresponding behavior exists.

**Exit evidence:** portable checks and explicitly authorized Linux/Lima acceptance
pass, with no unaccounted requirement. Report blockers honestly rather than declaring
completion when privileged or release verification is still outstanding.

## Verification and scope boundaries

For each implemented phase, run focused tests first, then applicable
`scripts/verify.sh quick`, `scripts/verify.sh quality` and
`scripts/verify.sh portable`. Use Node 24 as documented in
[the test procedure](../../docs/test-procedure.md). Run ShellCheck on touched shell
scripts and record the exact command. Confirm the selected engine before Docker
checks; select full/release checks when their surfaces are affected.

Privileged tests require explicit authorization for this work and run only on native
Linux or the GraphX Lima guest. No privileged sockets are forwarded to macOS. Report
native/macOS, container, Lima and Linux evidence separately; QEMU is not required.
Generated evidence belongs in `outputs/four-radio-vita/` or guest-local
`/var/lib/graphx`, not hand-edited maintained fixtures.

Initial acceptance excludes physical radios, GPS discipline, noise/AGC/filter
simulation, standalone non-burst transmit mode, automatic retuning, automatic
container restart, individual-container recovery and persistent recording. Preserve
future integration through explicit contracts without implementing unused services
or feedback edges. Unit tests of setting changes do not imply a live retuning loop.
