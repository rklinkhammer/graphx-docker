# GraphX implementation work package

You are the implementation agent for the GraphX project located at:

`~/workspace/graphx-docker`

## Assignment

Implement **Phase 11: UDP edges**, including IPv4 unicast, broadcast, and multicast transports and small runnable examples.

This phase follows the completed production-readiness sequence:

1. Configuration schema, loader, validation, and transport factory.
2. Runtime lifecycle, bounded queues, cancellation, reconnect, and graceful shutdown.
3. Protocol specification, compatibility rules, and message/trace identities.
4. CI, sanitizers, fuzzing, static analysis, and expanded transport tests.
5. Authentication, TLS, API validation, and container hardening.
6. OpenTelemetry integration, health checks, SLOs, and operational dashboards.
7. Durable or backend-driven telemetry history.
8. Authorized control plane and real runtime controls.
9. PCAPNG, Wireshark dissector, and extcap implementation.
10. Release engineering, compatibility policy, packaging, and support processes.
11. UDP unicast, broadcast, and multicast edges.

Do not implement the proposed SDR topology in this phase. Phase 11 provides the transport foundation and simple examples that the SDR example will use later.

## Objective

Add a production-quality UDP transport without weakening existing GraphX behavior. A UDP datagram carries exactly one complete GraphX framed envelope. UDP loss, duplication, and reordering must be observable and documented, and malformed traffic must not terminate a healthy receiver.

Initial protocol scope is IPv4. Support:

- unicast between one sender and one receiver;
- limited or directed IPv4 broadcast;
- IPv4 multicast group transmission and membership;
- explicit interface selection, socket buffers, multicast TTL, and multicast loopback;
- deterministic receive timeout, cancellation, and resource cleanup;
- packet capture and Wireshark inspection;
- minimal unicast, broadcast, and multicast examples.

IPv6, reliable UDP, retransmission, forward-error correction, message fragmentation/reassembly, DTLS, native one-to-many graph edges, and the SDR application are explicitly out of scope.

## Working rules

- Treat the repository as authoritative. Inspect `AGENTS.md`, repository status, build instructions, configuration, tests, packaging, and transport implementations before editing.
- Preserve unrelated user changes and the behavior of TCP, Unix-domain, in-process, and shared-memory transports.
- Keep GraphX independent of Docker, Compose, Open vSwitch, telemetry vendors, and GUI frameworks.
- Do not silently change the envelope wire format or configuration version.
- Do not treat UDP timeout, cancellation, malformed input, or socket failure as equivalent outcomes.
- Do not add fake behavior or claim delivery guarantees UDP cannot provide.
- Keep memory, datagram size, logging, error reporting, and malformed-packet processing bounded.
- Add dependencies only when justified; prefer the platform socket API and existing project facilities.
- Do not commit, push, publish, deploy, or contact external systems unless explicitly instructed.

## Required design decisions and invariants

1. A UDP edge uses the existing `ConnectionMode`: `connect` creates a sender and `listen` creates a receiver. The name `connect` does not imply a session or delivery guarantee.
2. Each UDP datagram contains exactly one existing `u32be` GraphX frame: the four-byte frame length followed by one serialized envelope.
3. GraphX does not divide an envelope among datagrams and does not reassemble IP or application fragments.
4. The encoded frame must not exceed `max_datagram_bytes` or the IPv4 UDP maximum of 65,507 bytes.
5. A receiver detects kernel-reported truncation, length mismatch, malformed envelopes, unsupported envelope versions, and trailing bytes before publishing a message.
6. Invalid datagrams are dropped, counted, and reported through bounded diagnostics; the receiver remains usable.
7. UDP has no reconnect, acknowledgement, retransmission, backpressure, TLS, or end-of-stream semantics.
8. `close()` is idempotent and promptly wakes a blocked receiver. Destructors and move operations must not leak resources or throw.
9. A multicast GraphX edge remains one logical producer-to-consumer edge. A diagnostic second listener may demonstrate network fan-out, but native graph fan-out is deferred.
10. Socket buffer configuration records the requested value and tolerates platform-specific kernel adjustment rather than requiring an exact read-back value.
11. Tests must not send broadcast traffic onto the developer's physical network. Use loopback where valid and isolated Linux namespaces or container networks for broadcast tests.

## Configuration contract

Add `udp` to `TransportKind`, `to_string()`, the strict configuration schema, CLI inspection, the transport factory, public headers, build/install manifests, and package-consumer validation.

The intended YAML form is:

```yaml
graph:
  edges:
    - { id: messages, from: sender.out, to: receiver.in, transport: udp }

transport:
  udp:
    messages:
      mode: unicast
      destination: 127.0.0.1
      bind: 0.0.0.0
      port: 7101
      interface: ""
      ttl: 1
      loopback: true
      reuse_address: false
      receive_buffer_bytes: 4194304
      send_buffer_bytes: 4194304
      max_datagram_bytes: 65507
      framing: u32be
```

Required fields are `mode`, `destination`, `bind`, and `port`. Other fields have documented defaults. If repository conventions justify slightly different names, record the reason in an ADR and update every example, test, and document consistently.

Validation must reject:

- unknown UDP keys or modes;
- missing or invalid IPv4 destinations and bind addresses;
- port zero or a port above 65,535;
- multicast mode with a non-multicast destination;
- unicast mode with a multicast or broadcast destination;
- broadcast mode with a multicast destination;
- TTL outside 0 through 255;
- unreasonable or overflowing socket-buffer values;
- `max_datagram_bytes` outside the supported framed-envelope range;
- any framing other than `u32be`;
- TCP-only settings such as reconnect, retry, and TLS;
- UDP entries that do not correspond to UDP graph edges.

The `interface` value is optional. When present, resolve either a valid interface name or IPv4 interface address consistently on Linux and macOS. A multicast receiver joins `destination` on that interface; a multicast sender uses it for outbound traffic. Invalid or unavailable interfaces must produce an actionable error.

## Implementation requirements

### UDP transport

Create a public `UdpTransport` and options type following the existing transport conventions. Integrate it through `TransportFactory` rather than constructing it directly in normal applications.

Sender requirements:

- serialize one framed envelope into one bounded buffer;
- reject oversize frames before calling the network;
- use one datagram send operation and reject a partial result;
- enable `SO_BROADCAST` only in broadcast mode;
- configure multicast interface, TTL, and loopback in multicast mode;
- provide useful errors containing the edge ID and operation without exposing payload data;
- emit existing send/error observability events.

Receiver requirements:

- set reuse and buffer options before binding where the platform requires it;
- join and leave multicast membership correctly;
- receive in a way that detects `MSG_TRUNC` or its portable equivalent;
- validate the outer frame length before allocating or deserializing;
- use a bounded buffer no larger than the configured datagram maximum;
- continue safely after invalid traffic while respecting the original receive deadline;
- avoid an unbounded CPU/logging loop under a flood of malformed datagrams;
- return only `message`, `timeout`, or `cancelled`; UDP does not return `end_of_stream`;
- unblock promptly on `close()` without relying on an arbitrary long polling delay.

### Observability

Extend observability with low-cardinality UDP counters or equivalent evidence for:

- datagrams and bytes sent;
- valid datagrams and bytes received;
- malformed datagrams;
- truncated datagrams;
- oversized outbound frames;
- socket errors;
- sequence gaps, duplicates, and out-of-order envelopes when those can be inferred from envelope metadata.

If the `TraceSink` interface is extended, use default no-op virtual methods or another source-compatible mechanism. Rate-limit repetitive error text while continuing to increment counters. Document that sequence-gap metrics are estimates because delayed or reordered packets can arrive later.

### Capture and Wireshark

Make UDP GraphX envelopes inspectable with the existing capture strategy. Update the Lua dissector and its tests as needed to support UDP `Decode As`, a configurable port/range preference, or a conservative magic-based heuristic. The dissector must reject datagrams whose framed length does not match the UDP payload length. Do not register broad UDP ranges by default in a way that misclassifies unrelated protocols.

### Examples

Provide three small examples. Reuse one publisher/subscriber implementation when practical rather than copying application logic.

1. `examples/udp-unicast`: a counter publisher sends numbered messages to one loopback subscriber.
2. `examples/udp-broadcast`: a discovery beacon sends small announcements to a listener on an isolated Docker or Linux-namespace subnet. Scripts must refuse unsafe ambiguous targets and clean up after interruption.
3. `examples/udp-multicast`: a publisher sends to an administratively scoped group such as `239.255.42.1`; a GraphX subscriber and optional diagnostic subscriber demonstrate group delivery with local loopback enabled.

Each example requires a minimal `graphx.yaml`, README, bounded runtime, deterministic success condition, nonzero exit on failure, and cleanup instructions. Privileged Linux requirements must be explicit. Examples must not require internet access.

## Acceptance requirements

Use these identifiers unchanged in the handoff and tests:

- **UDP-001 — Configuration:** Valid unicast, broadcast, and multicast configurations load; invalid mode/address/port/interface/buffer/size/framing combinations fail with precise paths.
- **UDP-002 — Unicast:** A factory-created sender and receiver exchange framed envelopes over IPv4 loopback with correct content and observability.
- **UDP-003 — Broadcast:** An isolated Linux acceptance test proves a broadcast sender reaches a listener without using the physical network.
- **UDP-004 — Multicast:** A multicast publisher sends one datagram that is received by at least two joined listeners in a supported test environment.
- **UDP-005 — Datagram integrity:** Oversize, truncated, malformed, unknown-version, length-mismatch, and trailing-data packets are rejected without delivering partial messages or permanently disabling the receiver.
- **UDP-006 — Lifecycle:** Timeout, cancellation, repeated close, destruction, and move behavior are deterministic and leak-free; blocked receive is promptly cancelled.
- **UDP-007 — UDP semantics:** No retry, reconnect, TLS, end-of-stream, or delivery guarantee is exposed; behavior under loss, duplication, and reordering is documented and tested where controllable.
- **UDP-008 — Observability:** Required counters and bounded diagnostics distinguish valid traffic, drops, truncation, oversize sends, sequence anomalies, and socket failures.
- **UDP-009 — Capture:** A captured UDP frame is decoded correctly by the GraphX Wireshark dissector, and malformed length cases are rejected.
- **UDP-010 — Examples:** All three examples validate, run according to their documented environment requirements, terminate deterministically, and clean up.
- **UDP-011 — Compatibility:** Existing configuration, public API, wire fixtures, transports, applications, packaging, and tests remain compatible.
- **UDP-012 — Quality gates:** Clean build, complete CTest, configured sanitizer/static-analysis/format checks, package consumer test, and affected container checks pass, or environmental restrictions are recorded without claiming success.
- **UDP-013 — Documentation:** README, configuration reference, protocol/operations guidance, and example documentation accurately state UDP limits, MTU/fragmentation risk, socket-buffer behavior, firewall/broadcast/multicast requirements, and platform limitations.

## Required tests

At minimum add:

- parser positive and negative cases for every UDP field and mode;
- factory validation and round trips;
- zero/small/maximum configured payload boundaries;
- timeout and close-during-receive tests without long sleeps;
- malformed, truncated, length-mismatch, unknown-version, and trailing-data injection using raw sockets;
- recovery by receiving a valid packet after each invalid case;
- multicast join/leave, explicit interface, loopback, and two-listener delivery;
- isolated broadcast delivery and teardown on Linux;
- metrics and trace assertions;
- repeated construction/destruction and file-descriptor leak checks where practical;
- Wireshark field/decoding regression tests;
- installation and external consumer compilation using the new public header.

Keep timing assertions tolerant enough for CI but bounded tightly enough to detect cancellation regressions.

## Verification before handoff

Run every feasible project check, including:

- clean CMake configure and native build;
- complete CTest suite;
- targeted UDP tests repeatedly;
- ASan/UBSan and other configured sanitizers;
- clang-tidy and explicit project formatting tools;
- package installation and consumer build;
- Compose validation and affected container builds;
- unprivileged unicast and multicast examples;
- privileged isolated broadcast/network tests on Linux;
- capture and `tshark` dissector verification.

Do not report a skipped check as passing. Distinguish code inspection, simulated testing, Docker testing, and actual privileged Linux runtime verification.

## Deliverables and handoff

Create `phase_11_handoff.md` containing:

1. Outcome summary.
2. A UDP-001 through UDP-013 matrix with implementation evidence and test evidence.
3. Architecture and compatibility decisions.
4. Files and public interfaces changed.
5. Exact commands and results.
6. Tests skipped and the precise environmental reason.
7. Known limitations and deferred scope.
8. Security and denial-of-service considerations.
9. Risks the verifier should examine closely.
10. Confirmation that the SDR topology was not implemented in this phase.

The phase is complete only when all acceptance requirements are met and the handoff accurately distinguishes verified runtime behavior from inspection or unavailable tests.
