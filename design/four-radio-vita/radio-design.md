# Standalone radio design (P1)

Status: implementation in progress; no final acceptance claim. P1 is an opt-in
standalone build, not an OCI image release. P5 remains responsible for published
image/catalog integration. The authoritative normalized node contract is reused.

See [P1.1 command analysis](command-analysis.md) for the upstream example review,
status/acknowledgment corrections and capability-query findings. The selected
contract below is the application migration target; the current standalone
executable still requires adapter integration and independent verification.

## Device and signal

One radio owns one `SoapySDR::Device` subclass, one RX CS16 stream and one serialized
control state. RF and signal frequencies are 1 MHz–6 GHz; integral sample rates are
1 ksample/s–2 Msample/s; bandwidth is positive and at most the rate; gain is -60–60 dB.
Amplitude is 0.25 full scale and initial phase is zero. Quantize using round-to-nearest
and saturate to [-32768,32767], counting clipped components. Out-of-passband samples
are valid zeros. The Nyquist edge is excluded. Phase advances through filtered samples
and is continuous across packets/bursts and configuration changes. A new scheduled
start begins a new sample epoch; duplicate starts never restart acquisition.

## Dependencies and reproducibility

Retain the pinned SoapySDR device dependency. Use `vrt_framework` as the sole
VITA codec and protocol runtime through `vita::core`; consume a verified immutable
commit `60a290c9b1da2396d3d704ebe52ea6cbcf2fa398`, with gates recorded in the
[implementation plan](implementation-plan.md#verified-library-migration-pin).
Its `graphx_radio` profile owns packet layouts,
command validation, execution/state acknowledgments, capabilities and TCP framing.
GraphX supplies bounded device and transport bindings. Remove vrtgen generation,
Python generator dependencies and generated VITA classes during the application
integration, updating notices, dependency locks and the SBOM together.
The current standalone executable still requires this integration; its existing
wire tests do not prove acceptance of the selected library profile.

## Packet profile

The library profile is the wire authority. Data: stream ID 1–4, present
Class ID `00 FF FF FF 00 00 00 00` (unknown OUI, unspecified class codes), UTC-encoded simulated integer seconds, real-time picosecond fraction,
I then Q signed big-endian components and one required trailer with enabled sample-frame
SSI. SINGLE/FIRST/MIDDLE/FINAL map to 0/1/2/3. Header count wraps modulo 16.
Sample counts derive from packet size minus the selected prologue and trailer.
Emit context before data and at each burst boundary with RF frequency, bandwidth,
sample rate and gain. Full packets have 1024 pairs; the final packet is short when
needed. Configurable burst length is 1–262144 pairs, default 262144.

Context and Command omit Class ID. Context contains exactly bandwidth, RF reference
frequency, gain and sample rate (52 bytes). Control uses library VITA command/ack packets, TLS, numeric controller ID 1 and
controllee/stream ID matching the radio. Message IDs are scoped to the running radio.
Configure carries all four settings, executed atomically; partial application is not
permitted. NO_ACTION requests status using CIF0/CIF1 selectors without setting-value
payloads; the state acknowledgment returns only selected fields.
Empty selectors and execute-style value payloads are rejected. EXECUTE with discrete
stream-enable sets start or stop. Requested AckV reports validation/scheduling admission, never execution. AckX is
emitted only after the device action completes, with actual effective time. AckS
reports observation time and current values. EXECUTE requesting state must request
ReqX+ReqS, returned in that order; EXECUTE+ReqS alone is rejected. NO_ACTION+ReqS
remains a read-only status/capability request. Timestamp presence alone never
schedules an action; start uses the CAM timestamp-control mode.
Reject unsupported fields, malformed layouts, nonfinite settings and mismatched IDs.
Bound packets to 1024 bytes, pending command work and replay history; identical IDs
with different content are errors. TLS peers are restricted by staged test/production
trust and controller identity. No unencrypted fallback.

## Time and pacing

Scheduled time uses UTC seconds/picoseconds as the simulated first-sample epoch and
is mapped once to the host steady clock using the current system-clock delta. The
standalone controller schedules 250 ms ahead; accept 20 ms–10 s lead. Reject late
commands. Sample timestamps use integer cumulative sample arithmetic, not arrival
or wall-clock reads. Pacing uses the steady clock; batching is bounded, acquisition
never waits for a UDP receiver. Test first emission within 100 ms after its deadline
on the development host; report observed timing, not hardware synchronization.
Control remains serviced during streaming. SIGINT/SIGTERM stop bounded work.

## Evidence scope

Standalone tests use local sockets and normalized fixtures. They prove device,
wire, control and process behavior, not OVS MTU, container capabilities or Linux
path acceptance. Real image packaging and privileged network tests are later phases.

## Implemented integration details and current limits

- `GRAPHX_BUILD_VITA_RADIO` is off by default. P1's native type and raw wire schema
  live under `config/vita/`; the harness builds a private catalog and invokes the
  authoritative normalizer. Existing published catalogs/images are unchanged.
- Amplitude and phase are authored as `amplitude_ppm` and `initial_phase_mdeg`;
  their defaults are 250000 and zero. Frequency, rate, bandwidth and gain are applied
  by the authenticated initial command. Reconfiguration is rejected while armed or
  streaming; device-level setters preserve phase and are independently testable.
- One TLS connection is accepted at a time, with 2-second handshake/frame/idle and
  pending-output deadlines. Commands are at most 1024 bytes; input/output bounds are
  2048/4096 bytes. There are 256 replay entries and monotonically increasing nonzero
  message IDs; an evicted/changed operation ID cannot apply a setting twice.
- Staged credential generation hashes are checked before and after TLS context
  loading. The trusted controller certificate must have DNS identity `controller`;
  controller ID is 1 and the resolved control source address is enforced. TLS
  credential rotation during the run is not implemented in this P1 server.
- Configuration returns requested AckV followed by terminal AckX and requested
  AckS. Scheduled start returns early AckV; AckX follows activation, and does not
  claim that a receiver has received IQ. Invalid settings identify the affected
  field; every setting is validated before any physical effect. A batch failure
  with uncertain hardware effects faults the stream rather than claiming rollback.
- Supported-limit queries use NO_ACTION selectors with CIF7 Maximum/Minimum
  (`0x0c000000`), returned in AckS independently of current settings. Section 9
  explicitly permits hardware-supported limits. Global bandwidth bounds do not
  override `0 < bandwidth <= sample_rate`. CIF7 Precision is not a device step.
  Unknown selectors report unsupported diagnostics; queries cannot change the
  sample epoch, phase, settings, scheduled operations or streaming state.
- The event loop services acquisition/control without blocking on a receiver. A
  pacing delay above 100 ms skips overdue samples, advances waveform phase and
  reports dropped samples. UDP send errors are counted separately. One heartbeat
  and bounded summary log per second avoid per-packet logging.
- The P1 local sender binds its resolved source address with a kernel-selected UDP
  source port. A production explicit source-port contract remains to be integrated;
  the destination port is resolved and fixed. Do not claim full authored source-port
  control or OVS attachment binding from loopback acceptance.
