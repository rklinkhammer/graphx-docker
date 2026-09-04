# GraphX independent verification work package

You are the independent verification agent for the GraphX project located at:

`~/workspace/graphx-docker`

## Assignment

Verify **Phase 11: UDP edges** independently against the repository and the acceptance contract below.

The implementer handoff is `phase_11_handoff.md`. Write the final verification report to `phase_11_verification.md`.

The phase covers IPv4 UDP unicast, broadcast, and multicast edges plus minimal examples. The SDR topology, IPv6, reliable UDP, fragmentation/reassembly, DTLS, and native one-to-many graph edges are outside this phase.

## Independence rules

- Read `prompt/implement.md` in full before verification.
- Do not assume the handoff, comments, documentation, or test names are accurate.
- Inspect repository status and preserve unrelated user changes.
- Map every acceptance requirement to direct implementation and runtime evidence.
- Verify behavior through adversarial execution wherever feasible, not code inspection alone.
- Distinguish pre-existing limitations from regressions.
- Do not lower criteria because a capability is difficult to test.
- Do not make product changes. Report findings with precise remediation unless explicitly authorized to fix them.
- Do not commit, push, publish, deploy, contact external systems, or send traffic onto a physical broadcast network.

## Required invariants to verify

1. One UDP datagram contains exactly one complete existing `u32be` GraphX framed envelope.
2. UDP does not change the envelope wire format or configuration version.
3. UDP never presents reconnect, retry, acknowledgement, TLS, backpressure, or end-of-stream semantics.
4. Invalid traffic cannot produce a partial message, unbounded allocation, unbounded logging, a permanent receiver failure, or an uninterruptible loop.
5. All buffers and datagrams are bounded before allocation and serialization.
6. Timeout and cancellation remain distinguishable; `close()` promptly cancels a blocked receive and is idempotent.
7. Broadcast tests are isolated from physical networks.
8. Multicast uses explicit group membership and sends one packet that can reach multiple listeners.
9. The graph data model remains one logical source-to-destination edge; the implementation does not silently invent ambiguous graph fan-out.
10. Existing transports and packages remain compatible.

## Acceptance contract

Verify every identifier independently:

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

## Verification procedure

### 1. Scope and change audit

- Inspect repository status, diff, new files, generated artifacts, and ignored files.
- Confirm changes are limited to UDP and necessary integration.
- Confirm no SDR topology, unrelated refactor, or later capability was introduced.
- Compare configuration and documentation claims directly with code behavior.

### 2. Clean build and baseline regression

- Configure and build from a new build directory.
- Run the complete CTest suite with failure output.
- Repeat multicast, cancellation, and timing-sensitive tests.
- Run configured ASan/UBSan, static-analysis, formatting, package, release, and container checks.
- Record exact commands, environment, counts, duration, and results.
- Never substitute an existing build cache for a required clean build.

### 3. Configuration adversarial testing

Independently exercise:

- missing, unknown, wrongly typed, empty, and oversized values;
- malformed and non-IPv4 addresses;
- multicast/unicast/broadcast mode-address mismatches;
- ports at 0, 1, 65,535, and 65,536;
- TTL boundary and out-of-range values;
- buffer and datagram sizes at boundaries and overflow values;
- invalid interface names and unavailable interface addresses;
- non-`u32be` framing and TCP-only keys;
- orphaned UDP entries and transport-kind mismatches.

Confirm diagnostics contain precise configuration paths and do not silently apply unsafe fallbacks.

### 4. Wire and receive-boundary testing

Use raw UDP sockets, not only the GraphX sender, to inject:

- empty and shorter-than-prefix datagrams;
- declared lengths smaller and larger than actual data;
- a datagram larger than the receive buffer;
- invalid magic and unsupported envelope versions;
- valid content with trailing bytes;
- a maximum accepted frame and an outbound frame one byte too large;
- bursts of malformed datagrams followed by a valid datagram.

Confirm invalid traffic is dropped and counted, the original deadline is preserved, log volume is bounded, and a subsequent valid packet is delivered.

### 5. Mode-specific runtime verification

For unicast, prove a factory-created sender and receiver round trip content over IPv4 loopback. Verify timeout, metrics, unavailable-destination behavior, and errors.

For broadcast, use a disposable Linux network namespace, veth pair, or isolated container network. Prove delivery, prove the example cannot target an unresolved physical network, interrupt setup, and confirm cleanup is repeatable and idempotent.

For multicast, join two independent listeners to the configured group and interface. Prove one transmitted datagram reaches both exactly once in the controlled test. Exercise loopback where deterministic, invalid membership, join/leave, and closing one listener without affecting the other.

### 6. Lifecycle and resource testing

- Distinguish timeout from cancellation.
- Close a receiver blocked with an infinite timeout and measure prompt return.
- Repeat close, destruction, construction, and teardown.
- Exercise move construction and assignment.
- Check file-descriptor counts where practical.
- Look for races with concurrent send, receive, and close.
- Run ThreadSanitizer when configured and supported; record exclusions precisely.

### 7. Observability and denial-of-service review

- Verify counter names, units, reset behavior, and label cardinality.
- Confirm malformed, truncated, oversize, sequence-gap, duplicate, out-of-order, and socket-error categories are distinguishable.
- Confirm telemetry failure cannot block UDP processing.
- Flood malformed packets in a controlled local test and check bounded memory, CPU behavior, and log rate.
- Confirm payload data and secrets are not logged.
- Record that UDP source addresses are spoofable and UDP lacks confidentiality and authentication.

### 8. Capture and dissector verification

- Capture a real GraphX UDP exchange into PCAPNG.
- Load the dissector with `tshark` and verify expected GraphX fields.
- Verify UDP `Decode As`, preference, or heuristic behavior matches documentation.
- Confirm malformed length and unrelated UDP payloads are not misclassified.
- Confirm existing TCP/custom-linktype capture tests still pass.

### 9. Example and documentation verification

- Validate every example configuration.
- Run unicast and multicast examples unprivileged where supported.
- Run broadcast only in its documented isolated environment.
- Confirm bounded completion, meaningful output, nonzero failure exits, signal handling, and complete teardown.
- Follow each README literally from a clean state.
- Check main documentation for UDP guarantees, MTU risk, fragmentation, buffers, firewall behavior, multicast/IGMP, broadcast boundaries, Docker/macOS/Linux limitations, and real-application guidance.

### 10. Compatibility and packaging

- Run all pre-existing transport and golden-wire tests.
- Install to a staging prefix.
- Compile and run an external consumer that includes the UDP public header and uses the factory.
- Inspect installed files and package metadata.
- Confirm existing valid configurations behave unchanged.

## Environmental evidence rules

Report each check as:

- **Runtime verified:** executed in the required environment.
- **Inspection only:** reviewed but not executed.
- **Not verified:** an explicit environmental restriction prevented the check.

Docker Desktop does not establish native Linux namespace, OVS, directed-broadcast, or physical multicast behavior. Do not treat macOS or Docker-only results as native Linux verification. Do not block unicast and loopback multicast checks merely because privileged Linux networking is unavailable.

If broadcast traffic cannot be isolated safely, do not run it. Record the exact missing capability and mark UDP-003 not verified rather than passed.

## Finding severity

- **P0:** Immediate security, data-loss, host-network, or catastrophic correctness issue.
- **P1:** Core acceptance criterion is unmet or implementation is unsafe for its stated use.
- **P2:** Significant reliability, compatibility, observability, operability, or maintainability defect.
- **P3:** Non-blocking improvement.

Every finding must include severity, requirement ID, exact location, reproduction or evidence, expected and actual behavior, impact, remediation, and a required regression test.

## Verdict rules

Return exactly one verdict:

- **ACCEPTED:** UDP-001 through UDP-013 pass; only non-blocking P3 findings remain.
- **CHANGES REQUIRED:** one or more P1/P2 findings or acceptance failures remain.
- **REJECTED:** the design is unsafe, fundamentally incompatible, or does not implement the work package.
- **BLOCKED:** external restrictions prevent enough required verification to reach a responsible verdict. Do not use this merely because one environment-specific test is unavailable.

An unavailable native Linux broadcast test normally makes UDP-003 not verified and prevents `ACCEPTED`. Decide between `CHANGES REQUIRED` and `BLOCKED` based on whether implementation defects were also found and whether verification can reasonably continue on a suitable host.

## Report format

Write `phase_11_verification.md` with:

1. Verdict.
2. Executive summary.
3. Environment and repository state.
4. UDP-001 through UDP-013 matrix containing requirement, implementation evidence, runtime evidence, status, and remediation.
5. Findings ordered by severity.
6. Exact tests and checks run with results.
7. Runtime-verified, inspection-only, and unverified areas.
8. Protocol, compatibility, security, and denial-of-service assessment.
9. Documentation and example assessment.
10. Required remediation before acceptance.
11. Readiness for the SDR topology phase.

Do not recommend starting the SDR topology until Phase 11 is accepted.
