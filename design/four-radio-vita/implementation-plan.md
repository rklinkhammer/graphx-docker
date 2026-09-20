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
| P1.1 | Library profile/API audit and normative mappings | Library contract implemented and pinned below; application acceptance pending |
| P1.2 | Library NO_ACTION status and correlated ReqX+ReqS; no generated protocol engine | Application migration and independent interoperability verification required |
| P1.3 | Atomic device batch with field-specific diagnostics and no partial application | Bind the same Soapy device; verify failures and unknown physical effects |
| P1.4 | CIF7 supported min/max query/response distinct from current state | Semantics established by VITA Section 9 and 9.12; integration acceptance pending |
| P1.5 | Independent Context/ack vectors, non-divisor time, replay exhaustion, stalled-peer bounds | Library gates and host adapter evidence required |

P1.4 uses CIF7 Maximum/Minimum, in that wire order, with Current absent.
Global bandwidth limits remain distinct from `BW<=Fs` and device constraints.
CIF7 Precision is not a supported increment. Public device increments/choices must
be enforced by the same admission owner; do not invent a wire attribute for them.

P1.5 proceeds independently of P1.4. No OVS deployment or privileged permission is
needed for these local tests. P1 cannot close while capability queries remain
unimplemented; do not replace that requirement with status or static documentation.

### Verified library migration pin

Use `vrt_framework` commit `60a290c9b1da2396d3d704ebe52ea6cbcf2fa398` for
this migration. Its `docs/implementation/P17-graphx-profile.md` contains the public
APIs, host binding requirements and requirement-to-evidence matrix;
`docs/implementation/artifacts/P17/results.json` records all six passing gates:
macOS arm64 Debug (235), Release (239), ASan/UBSan (235), TSan (235), and Linux
arm64 Debug/Release (235 each). The accompanying source manifest and full logs
identify the tested implementation. Linux sanitizer gates were not run.

This pin qualifies the library, not the current GraphX executable. Replacing its
protocol engine and binding the same Soapy device and authenticated TCP transport,
updating dependency/release inventories, and independent application acceptance
remain P1 work. No privileged GraphX or OVS tests were run for this library migration.

### Design and dependency integration

- Create `design/four-radio-vita/radio-design.md` with the device API, lifecycle,
  packet layouts, timing model, limits and test approach. Include exact control/CAM
  mappings, applied acknowledgments, timed-start encoding, idempotency and context
  cadence. Choose deterministic defaults rather than reopening accepted behavior.
- Pin and verify SoapySDR and `vrt_framework`; remove vrtgen/code generation and
  consume the library through CMake `vita::core`. Record licenses, source notices, build
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

- Extend shared release/image roles with the verified SoapySDR, `vrt_framework`, FFT and
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
