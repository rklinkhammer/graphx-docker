# Four-radio VITA system architecture review

Date: 2026-09-15

## Scope

This is a read-only architecture review of
[`docs/vita_system.md`](docs/vita_system.md) against the repository's current
configuration, compilation, execution, ownership, OVS, telemetry, SDR, release and
verification contracts.

The review prioritizes contradictions, implementation blockers, underspecified
behavior and verification gaps. It does not authorize implementation,
infrastructure provisioning or privileged tests.

## Findings

### 1. Critical: degraded startup contradicts the current lifecycle

The brief requires starting the available subset when a radio fails and preserving
healthy components during startup
([vita_system.md](docs/vita_system.md#L697-L727)). The current Compose runner waits
for every application, rejects any exited container and rolls the entire graph back
on failure
([compose_execution.cpp](src/infra/compose_execution.cpp#L864-L902),
[compose_execution.cpp](src/infra/compose_execution.cpp#L937-L942)). The architecture
also defines startup as an all-listeners-ready transaction followed by one release
barrier
([GraphX_Architecture.md](docs/GraphX_Architecture.md#L225-L263)).

This must be resolved before implementation. Either:

- Limit degraded operation to failures occurring after successful graph startup; or
- Design a new optional-node/readiness-group contract, including barrier behavior,
  ownership state, status semantics and rollback rules.

Likewise, explicitly restoring one failed container while healthy components
continue is not currently a supported lifecycle operation. The current recovery unit
is the whole graph.

### 2. Critical: the mirror-fed recorder has no viable execution contract

The recorder must receive raw Ethernet frames through a dedicated mirror attachment
([vita_system.md](docs/vita_system.md#L460-L486)). GraphX currently creates the mirror
as a veth pair whose passive peer remains in the host network namespace
([endpoint_resources.cpp](src/infra/endpoint_resources.cpp#L590-L653)); captures
consume that peer through the privileged infrastructure lifecycle. It is not attached
to a container.

Application containers also run as UID 65532 with all capabilities dropped
([compile.cpp](src/compile.cpp#L150-L154)), and the executor rejects additional
Compose capability fields
([compose_execution.cpp](src/infra/compose_execution.cpp#L394-L435)). A recorder
cannot open an `AF_PACKET` raw socket under that policy.

The specification needs to select and threat-model one mechanism, most likely:

- Move the mirror peer into the recorder's verified network namespace.
- Add a reviewed recorder-specific capability profile granting only `CAP_NET_RAW`.
- Record container namespace and interface identities in the common ledger.
- Verify that the interface cannot transmit into the mirrored network, access other
  host interfaces or survive ownership cleanup.

### 3. High: the exact VITA profile remains a future design task

The document selects stream IDs, a zero class ID, 1,024 IQ pairs, timestamps and a
trailer, but still delegates essential wire decisions: component order and scaling,
packet field presence, context cadence, packet-count behavior, burst identity,
timed-start encoding, CAM policy, acknowledgment meaning and partial-application
behavior
([vita_system.md](docs/vita_system.md#L241-L338),
[vita_system.md](docs/vita_system.md#L552-L591)).

The cited `vrtgen` control example permits partial application and warnings, but the
brief correctly says not to inherit that policy. No replacement policy is selected.
The reviewed revision is also explicitly not a production dependency pin
([vita_system.md](docs/vita_system.md#L202-L225)).

Before coding, produce a normative profile containing:

- Exact generated packet definitions and checked-in generation inputs.
- Data, context, control and acknowledgment packet layouts.
- Timed-start representation and acknowledgment phase.
- Unsupported fields and packet types.
- Independent vectors or decoder selection.
- A verified `vrtgen` pin and LGPL-3.0 distribution plan for its header library and
  generated output.

### 4. High: jumbo frames cannot be requested through authored configuration

The brief correctly observes that a 4,096-byte IQ payload cannot cross a 1,500-byte
path without fragmentation
([vita_system.md](docs/vita_system.md#L340-L355)). Internally, GraphX already carries
and applies an endpoint MTU
([network.hpp](include/graphx/network.hpp#L60),
[endpoint_resources.cpp](src/infra/endpoint_resources.cpp#L358-L361)), but normalized
attachments default to 1500 when no field exists
([resolved_network.cpp](src/resolved_network.cpp#L95)). Authored attachment schema
does not expose `mtu`
([graphx.schema.json](config/schema/graphx.schema.json#L123-L195)).

The design must add an authoritative MTU contract to authored and normalized schemas,
validate path consistency, configure both veth ends and mirror peers, and verify the
effective MTU before releasing applications. The exact minimum should be derived from
the selected VITA layout rather than fixed at 9000 without calculation.

### 5. High: the FFT and feature-result wire/API contract is missing

The brief requires one jumbo UDP datagram containing power bins and extensive
metadata
([vita_system.md](docs/vita_system.md#L408-L441)). The current SDR processor emits
`RawPowerResult`, defined as newline-delimited JSON over TCP with a 4,096-byte maximum
([wire-schemas.json](config/catalog/wire-schemas.json#L20-L25)); its catalog output is
also TCP-only and single-connection
([sdr.processor.json](config/catalog/types/sdr.processor.json#L24-L37)).

An implementation cannot derive FFT length or validate requested bin width until the
specification selects:

- Power representation and byte order.
- Bin ordering and whether both positive and negative bins are transmitted.
- Window, overlap and normalization enums.
- Bounded gap metadata encoding.
- GraphX envelope presence.
- Exact serialized overhead and maximum FFT length.
- Feature-result telemetry/API shape and frontend presentation.

"Use existing observation/display facilities"
([vita_system.md](docs/vita_system.md#L445-L456)) is insufficient: telemetry currently
accepts a closed event vocabulary
([security.mjs](apps/telemetry/security.mjs#L14-L20),
[security.mjs](apps/telemetry/security.mjs#L153-L163)) and has no detected-frequency
result model.

### 6. High: several acceptance conditions have no measurable threshold

The verification matrix is broad, but implementation-specific limits are still
deferred. Missing values include:

- Scheduled-start skew and late-start tolerance.
- Incomplete-burst timeout and maximum in-flight bursts.
- UDP reorder window and duplicate policy.
- Control queue depth, connection limit, request deadline and retry schedule.
- FFT queue depth and prolonged-outage policy.
- Peak-estimation accuracy relative to bin width.
- Recorder acceptance traffic rate and acceptable observed drops.
- Pacing deadline threshold and overload response.

Examples include the deferred control bounds
([vita_system.md](docs/vita_system.md#L534-L541)) and undeclared start tolerance
([vita_system.md](docs/vita_system.md#L654-L672),
[vita_system.md](docs/vita_system.md#L794)). These need concrete defaults and hard
maxima before tests can distinguish success from merely producing traffic.

### 7. Medium: initial scope and future extensibility are interleaved

Feedback-driven retuning is explicitly deferred, but the document also says to
preserve the path and discusses its schema, authorization and arbitration
([vita_system.md](docs/vita_system.md#L680-L695)). That can cause implementers to add
an unused graph edge or premature API.

State plainly that the initial graph has no feature-detector-to-controller connection
and no automatic retuning behavior. Preserve extensibility through stream identity
and applied-setting metadata, not an inactive runtime path.

## Assessment

This is a strong architectural discovery brief. It preserves GraphX's authoritative
loader, OVS ownership, credential staging, raw VITA boundary, bounded resources and
honest evidence rules. It also correctly identifies current SDR behavior that cannot
simply be relabeled as VITA.

It is not yet implementation-ready. The design report requested in
[`vita_system.md`](docs/vita_system.md#L769-L778) should be treated as a mandatory
approval gate, not merely the first implementation artifact. That report should
resolve the seven findings above and split the work into at least four contracts:

1. VITA codec and virtual SoapySDR device.
2. Authored configuration, jumbo MTU and mirror-recorder lifecycle.
3. Four-stream FFT and feature-result protocol.
4. Startup, degraded-operation and recovery semantics.

No runtime or privileged tests were run for this review.
