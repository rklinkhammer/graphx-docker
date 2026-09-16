# Standalone radio design (P1)

Status: implementation in progress; no final acceptance claim. P1 is an opt-in
standalone build, not an OCI image release. P5 remains responsible for published
image/catalog integration. The authoritative normalized node contract is reused.

See [P1.1 command analysis](command-analysis.md) for the upstream example review,
status/acknowledgment corrections and capability-query findings. The implemented
command subset below is not yet the final independently verified wire profile.

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

SoapySDR 0.8.1 commit `1cf5a539a21414ff509ff7d0eedfc5fa8edb90c6` and vrtgen
`5e7497d24069c140be431d8468655f67d25f382d` are downloaded as SHA-256 checked archives.
The latter's Python generator requires setuptools 80.9.0 (its entry point imports
pkg_resources). Record Python tool versions alongside generation outputs. Generated
packet classes use the upstream support headers; there is no proprietary VITA codec.
Preserve upstream notices and include dependency sources/licenses with distribution.
This is implementation-source validation, not certification against a supplied VITA
standard. Generator/backend verification is distinct from independent wire decoding.

## Packet profile

`config/vita/radio.yaml` is the generation input. Data: stream ID 1–4, present zero
class identifier, UTC-encoded simulated integer seconds, real-time picosecond fraction,
I then Q signed big-endian components and one required trailer with enabled sample-frame
SSI. SINGLE/FIRST/MIDDLE/FINAL map to 0/1/2/3. Header count wraps modulo 16.
Sample counts derive from packet size minus the selected prologue and trailer.
Emit context before data and at each burst boundary with RF frequency, bandwidth,
sample rate and gain. Full packets have 1024 pairs; the final packet is short when
needed. Configurable burst length is 1–262144 pairs, default 262144.

Control uses generated VITA command/ack packets, TLS, numeric controller ID 1 and
controllee/stream ID matching the radio. Message IDs are scoped to the running radio.
Configure carries all four settings, executed atomically; partial application is not
permitted. NO_ACTION requests status using CIF0/CIF1 selectors without setting-value
payloads; a separate generated query acknowledgment returns only selected fields.
Empty selectors and execute-style value payloads are rejected. EXECUTE with discrete
stream-enable sets start or stop. An execution request returns an execution reply;
a status request returns status; requesting both returns execution then status as
two VITA packets on the same TLS stream. Status acknowledges current values; capability ranges are the published
profile bounds. Timed start uses the CAM timestamp-control mode, never timestamp
presence alone. Receive/schedule acknowledgments are not execution acknowledgments.
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
- Configuration returns execution and/or applied state as requested; a timed-start acknowledgment means scheduled,
  not already emitting. Subsequent status indicates running; data proves execution.
  Status does not reset streaming. Invalid settings return errors for the affected
  RF-frequency, sample-rate, bandwidth or gain field. Nonintegral rates report
  unsupported precision, and late starts report a timestamp problem with the
  timing-issues mode. All validation precedes device mutation. Malformed framing
  or unknown layouts close the session. State/operation errors retain a generic
  field-not-executed response and are not represented as successful application.
- Capability ranges are queryable through the actual SoapySDR device API and recorded
  in this fixed profile. A VITA wire capability-range query is not implemented yet;
  do not describe the status response as dynamic capability discovery.
- The event loop services acquisition/control without blocking on a receiver. A
  pacing delay above 100 ms skips overdue samples, advances waveform phase and
  reports dropped samples. UDP send errors are counted separately. One heartbeat
  and bounded summary log per second avoid per-packet logging.
- `scripts/vita/generate.py` fixes upstream error-field member declaration guards
  and an acknowledgment action-mode declaration mismatch in a build-local copy of
  the pinned templates. macOS supplies a three-function byte-swap compatibility
  header. Generated files are never hand-edited. Upstream notices are retained.
- The P1 local sender binds its resolved source address with a kernel-selected UDP
  source port. A production explicit source-port contract remains to be integrated;
  the destination port is resolved and fixed. Do not claim full authored source-port
  control or OVS attachment binding from loopback acceptance.
