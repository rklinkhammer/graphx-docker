# GraphX independent verification work package

You are the independent verification agent for the GraphX project located at:

`~/workspace/graphx-docker`

## Assignment

Independently verify **Phase 12: unified QEMU demonstrations** against `~/workspace/prompt/implement.md`, the repository, and the acceptance contract below.

The implementation must provide two demonstrations of the same raw TCP/UDP topology:

1. an external-QEMU profile suitable for macOS, Linux, and other documented platforms;
2. a Linux-only containerized-QEMU profile using KVM when selected and an explicit TCG mode.

Read `phase_12_handoff.md` if present, but do not treat it as proof. Write the final report to `phase_12_verification.md` in `~/workspace/graphx-docker`.

## Independence rules

- Read `~/workspace/prompt/implement.md` and applicable repository instructions in full before testing.
- Inspect repository status, diff, generated artifacts, ignored paths, and baseline history. Preserve unrelated user changes.
- Do not assume file names, documentation, topology labels, test names, logs, health status, or the handoff are accurate.
- Derive the acceptance matrix independently and map each status to direct code/configuration evidence plus runtime evidence where required.
- Exercise adversarial behavior and failure paths rather than relying only on happy-path scripts.
- Run profile scripts as documented before substituting manual commands; use manual probes to verify their claims.
- Distinguish external-host runtime, Docker runtime, guest runtime, packet observation, and GUI behavior.
- Do not make product fixes. Report precise remediation unless explicitly asked to implement corrections.
- Do not commit, push, publish, deploy, contact external systems, expose services, or send traffic outside controlled local networks.
- Do not lower Linux container criteria because verification is performed first on macOS. Do not lower external-profile criteria because Linux container mode works.

## System model to verify

Both profiles must expose this same acyclic logical topology:

```text
host-origin -- raw UDP and raw TCP --> qemu-node
qemu-node   -- raw UDP and raw TCP --> host-receiver
```

The expected intentional deployment difference is:

```text
External profile:   Docker services + host-managed QEMU process/guest
Container profile:  Docker services + Docker-managed QEMU process/guest
```

The verifier must reject an implementation that achieves apparent reuse by inaccurately representing deployment boundaries, fabricating GraphX message events, or silently using different guest/application logic.

## Required invariants

1. Both profiles use the same guest artifacts and application payload contract.
2. Application packets are ordinary TCP/UDP and do not require a GraphX envelope or `u32be` GraphX frame.
3. Raw edges are not constructed by the GraphX transport factory.
4. Existing framed GraphX TCP and UDP behavior is unchanged.
5. `host-origin` and `host-receiver` are distinct real services.
6. External QEMU is owned and terminated safely without broad process matching or stale PID trust.
7. Containerized QEMU runs without `privileged: true` in the default profile.
8. A mounted `/dev/kvm` is not sufficient proof of KVM; QEMU must report KVM active through a trustworthy mechanism.
9. QEMU process/container liveness is not sufficient guest readiness; the guest application must be probed.
10. Passive packet events remain distinguishable from GraphX message telemetry and packet history remains separate from GraphX message history.
11. Capture/history/parser/log/retry/startup/shutdown behavior is bounded.
12. GUI and API claims accurately reflect the selected profile, accelerator, state, and control capability.
13. Pause/resume controls the origin traffic generator only; guest control is not implied.
14. QMP, data ports, telemetry, and control surfaces are private or loopback-only by default.
15. The default profiles do not use physical broadcast/multicast, TAP, or uncontrolled host-network changes.

## Acceptance contract

Verify every identifier independently:

- **QEMU-001 — Shared logical topology:** Both profiles expose the same valid acyclic three-node, four-edge TCP/UDP topology and endpoint semantics.
- **QEMU-002 — Raw protocol integrity:** Application traffic is ordinary TCP/UDP with `framing: none`; raw edges never enter the GraphX transport factory and existing GraphX transports remain unchanged.
- **QEMU-003 — Shared artifacts and code:** Both profiles use the same guest image, guest application, host endpoint implementations, observer, GUI, and test payload contract; artifact identity is verifiable.
- **QEMU-004 — External deployment model:** The portable profile models and operates QEMU as an externally managed host node while Docker services remain managed.
- **QEMU-005 — External runtime:** A supported macOS or Linux host boots the guest, proves bidirectional TCP and UDP, reports the actual accelerator, and stops cleanly.
- **QEMU-006 — Container deployment model:** The Linux profile models QEMU as a Docker-managed service and accurately exposes the nested container/VM runtime.
- **QEMU-007 — Container runtime and KVM:** Linux boots the guest in the QEMU container, proves bidirectional TCP and UDP, verifies KVM when selected, supports explicit TCG, and requires no privileged container in the default profile.
- **QEMU-008 — Lifecycle and readiness:** Both profiles provide bounded, idempotent startup, guest-application readiness, failure rollback, restart, and shutdown without unrelated process termination or orphaned resources.
- **QEMU-009 — Passive live observation:** Packet-derived node/edge counters update through the API and WebSocket without representing packets as GraphX messages.
- **QEMU-010 — Capture:** Capture is enabled by default, bounded, valid as Ethernet PCAP/PCAPNG, cataloged/downloadable, secure against unsafe paths, and disableable.
- **QEMU-011 — Packet history:** Separate packet history is enabled by default, queryable, persistent according to policy, bounded by time/count/bytes, and disableable.
- **QEMU-012 — GUI parity and accuracy:** Both demos use one GUI, render topology and deployment boundaries accurately, update live, and remain functional across all tab transitions.
- **QEMU-013 — Scoped control:** Authenticated pause/resume affects only host-origin, reset semantics are accurate, observation remains appropriately accessible, and unsupported guest control is not implied.
- **QEMU-014 — Unified CLI:** Both profiles implement the shared start/status/verify/logs/token/stop workflow with actionable diagnostics and deterministic exits.
- **QEMU-015 — Security and resource bounds:** Default exposure is loopback/private, QMP is private, no Docker socket or privileged container is required, and all storage, parsing, retries, waits, payloads, and diagnostics are bounded.
- **QEMU-016 — Compatibility and quality:** Existing builds, tests, packaging, standard demo, UDP examples, telemetry security, GUI, capture/history, documentation, and supported platform workflows remain compatible.
- **QEMU-017 — Documentation:** A common guide plus profile-specific guides explain prerequisites, architecture, operation, capture/history, GUI, controls, acceleration, limitations, cleanup, and troubleshooting without conflating inspection and runtime proof.

## Verification environments and evidence classification

Record host OS, version, kernel, CPU architecture, virtualization flags, Docker/Compose, QEMU, compiler, CMake, Node, Python, tshark, SQLite, available memory/storage, `/dev/kvm` ownership/mode, and repository state.

Classify every check as one of:

- **Runtime verified — external profile**
- **Runtime verified — container TCG profile**
- **Runtime verified — container KVM profile**
- **Automated test with simulated dependency**
- **Inspection only**
- **Not verified — environmental restriction stated**
- **Failed**

Do not use one profile as runtime evidence for the other. Static reuse evidence may apply to both, but each deployment and lifecycle must be exercised separately. A macOS external run does not prove native Linux external host-gateway behavior. TCG does not prove KVM.

## Verification procedure

### 1. Scope, baseline, and reuse audit

- Inspect repository status, full diff, new files, generated outputs, ignores, image definitions, Compose files, scripts, and documentation.
- Establish a clean baseline build/test result without discarding current changes.
- Trace guest source to guest artifacts and both launch profiles.
- Compare artifact hashes used by both profiles.
- Search for copied/divergent guest, peer, observer, GUI, payload, port, retention, and lifecycle implementations.
- Confirm wrappers do not contain hidden duplicate implementations.
- Identify unrelated scope expansion, including TAP, physical networking, SDR behavior, or broad external-node orchestration.

### 2. Configuration and topology adversarial verification

Independently validate both `graphx.yaml` files and compare their normalized logical topology.

Exercise at least:

- missing and duplicate nodes/edges;
- graph cycles;
- wrong edge endpoints or direction;
- unknown runtime/lifecycle/data-plane values;
- `framing: none` without raw/external designation;
- raw edges with GraphX-only configuration;
- framed GraphX edges incorrectly marked raw;
- missing managed service images;
- an external node incorrectly requiring a managed service;
- a container QEMU node without its managed service;
- attempts to instantiate raw edges through the transport factory;
- topology/API serialization of runtime, execution owner, framing, observation source, and control capability.

Confirm existing valid configuration and framed TCP/UDP tests remain unchanged.

### 3. Shared guest and endpoint verification

- Rebuild guest artifacts from the documented clean procedure.
- Verify reproducible identity to the extent promised by the project; otherwise record expected nondeterministic fields.
- Prove both profiles select the same kernel/root filesystem/application artifacts.
- Capture deterministic TCP and UDP payloads in each direction.
- Compare sent and received bytes.
- Inspect wire bytes and prove no mandatory GraphX magic, envelope, or length prefix was introduced.
- Exercise zero/small/maximum supported payloads, malformed input, TCP reconnect, UDP loss tolerance, and repeated operation.
- Confirm host-origin and host-receiver are distinct running services rather than labels for one process.

### 4. External-profile runtime verification

On each available documented platform:

1. Start from stopped state with no stale project resources.
2. Run the documented build/start command literally.
3. Confirm supporting containers are running and QEMU is a host process owned by the invocation.
4. Confirm the topology/API reports QEMU as host/external, not as a Docker container.
5. Verify actual accelerator selection from QEMU evidence.
6. Prove guest-application readiness and bidirectional TCP/UDP.
7. Run status, verify, logs, token, pause/resume/reset, and stop.
8. Repeat start and stop.

Adversarially test:

- unavailable QEMU executable;
- missing guest artifact;
- occupied ports;
- unwritable output directory;
- unsupported accelerator request;
- stale PID file containing an unrelated live PID;
- stale QMP socket;
- guest boot failure;
- guest application readiness failure;
- supporting-container failure;
- interrupt during startup;
- forced QEMU exit;
- repeated and partial cleanup.

Confirm no broad `pkill`, unsafe PID reuse, unrelated process termination, orphan process, stale listener, or misleading success output.

### 5. Linux container-profile runtime verification

This procedure requires a native Linux Docker host. Validate the Compose model before runtime.

#### TCG profile

- Run explicitly with `--accel tcg`.
- Confirm the QEMU process and guest run inside `qemu-node`.
- Confirm all application nodes are distinct managed services.
- Prove guest readiness and bidirectional TCP/UDP.
- Exercise the complete shared CLI and stop/restart behavior.

#### KVM profile

- Record CPU virtualization capability and `/dev/kvm` permissions.
- Run explicitly with `--accel kvm`.
- Confirm only the intended device/group access was granted.
- Query QMP or trustworthy QEMU output to prove KVM is enabled.
- Repeat bidirectional TCP/UDP and lifecycle testing.
- Compare boot timing with TCG as informational evidence.

#### Accelerator failures

- Remove/deny `/dev/kvm` in a controlled test.
- Confirm `--accel kvm` fails clearly and nonzero.
- Confirm `--accel auto` either selects proven KVM or announces a TCG fallback.
- Confirm the GUI/API displays the actual accelerator rather than requested configuration.

Inspect the running container and Compose configuration. Fail QEMU-007 or QEMU-015 if the default profile uses `privileged: true`, mounts the Docker socket, publishes QMP externally, or grants unjustified devices/capabilities. Confirm the default profile does not require `/dev/net/tun` or `NET_ADMIN`.

### 6. Networking and isolation verification

For both profiles:

- Trace origin-to-guest and guest-to-receiver paths for TCP and UDP.
- Prove Docker service discovery or the documented relay is deterministic and does not depend on a changing container IP.
- Confirm a TCP-only relay is not incorrectly claimed to support UDP.
- Inspect listening ports on host and containers.
- Confirm default host publications bind only to loopback and only where required.
- Restart individual services and verify DNS/endpoint recovery.
- Inject destination and port configuration errors and confirm actionable failure.
- Confirm no packet is intentionally emitted onto a physical broadcast or multicast network.

### 7. Lifecycle, readiness, and resource verification

- Distinguish container/process liveness from guest-kernel boot and guest-application readiness.
- Verify readiness includes both required protocols or separately reports each.
- Measure and record bounded startup, health, retry, and shutdown deadlines.
- Exercise QMP/guest shutdown, timeout fallback, repeated stop, SIGTERM, forced termination, and host interruption.
- Restart QEMU while observer, telemetry, receiver, and GUI remain running; confirm recovery or accurate degraded state.
- Check for orphan processes, containers, networks, sockets, PID files, port listeners, corrupt captures, and locked databases.
- Confirm retained volumes/files match documented stop/reset policy.

### 8. Passive observation verification

Generate traffic with known counts and independently compare source, receiver, capture, observer, API, and GUI totals, allowing only documented timing/packet-loss behavior.

Inject or simulate:

- partial PCAP global header;
- partial packet record;
- invalid captured/original lengths;
- oversized record;
- unsupported link type;
- truncated Ethernet/IP/TCP/UDP packet;
- file truncation, replacement, and rotation;
- QEMU restart and capture-file recreation;
- unattributable traffic;
- malformed packet flood within a safe local bound.

Confirm memory, CPU, logging, and diagnostic retention remain bounded and subsequent valid packets are processed. Confirm API/WebSocket events identify packet observation and are not accepted as GraphX message-history records.

### 9. Capture verification

- Confirm capture is enabled by default and `--no-capture` disables it.
- Inspect real PCAP and generated PCAPNG with `capinfos`, `tshark`, or equivalent authoritative tooling.
- Confirm Ethernet link type 1 and expected TCP/UDP packets.
- Confirm incomplete active captures are not served unsafely.
- Exercise catalog refresh, download, rotation, restart persistence, count/byte limits, and cleanup policy.
- Re-run traversal, encoded traversal, symlink, non-regular-file, oversized-file, malformed-request, and race-resistant download tests.
- Confirm the GraphX Lua dissector is not claimed to decode raw QEMU application packets unless they independently match a supported protocol.

### 10. Packet-history verification

- Confirm history is enabled by default and `--no-history` disables it.
- Verify timestamp, direction, edge, protocol, addresses, ports, lengths, truncation, and bounded preview/hash fields.
- Confirm packet history uses separate schema/storage/API semantics from GraphX envelope history.
- Verify retention by age, record count, and database bytes during normal ingestion and after restart.
- Test concurrent query/ingest, malformed queries, pagination limits, oversized parameters, database corruption handling, and shutdown during writes.
- Confirm no secret/control token is persisted and payload previews follow documented bounds.

### 11. GUI and API verification

Exercise both profile configurations against the same GUI build.

Confirm:

- the three nodes and four raw edges render;
- Application and Network views distinguish logical from deployment structure;
- external mode shows host-managed QEMU;
- container mode shows the QEMU container, nested VM, and guest application;
- protocol labels say Raw TCP/Raw UDP rather than GraphX-framed transport;
- requested and actual accelerator are not confused;
- booting, ready, degraded, stopped, and unavailable states are accurate;
- counters update over WebSocket without browser refresh;
- reconnect after telemetry restart restores updates;
- all ordered transitions among Application, Network, History, and Capture work, especially History to every other tab;
- capture and history disabled states are understandable rather than blank;
- unsafe or malformed API/WebSocket requests do not terminate telemetry.

### 12. Control verification

- Confirm observation works under the documented local default without entering a control token.
- Confirm pause/resume/reset remain disabled until an authorized token is available where required.
- Reject missing, malformed, expired, or incorrect tokens.
- Pause origin and prove new origin traffic stops within the documented settling interval.
- Resume and prove traffic restarts without restarting the demo.
- Reset and verify exact counter/capture/history semantics.
- Confirm QEMU guest state is not shown as paused merely because origin traffic paused.
- Confirm the GUI does not offer unsupported guest application control.
- Repeat control after telemetry reconnect and service restart.

### 13. Unified CLI and documentation verification

Follow the common guide and both profile READMEs literally from clean state. Verify matching verbs, compatible common options, useful help, deterministic exits, clear profile/accelerator/artifact/retention/URL output, and useful diagnostics.

Confirm documentation covers:

- prerequisites and supported host/CPU combinations;
- external versus container deployment diagrams;
- shared topology and raw protocol boundary;
- build and artifact reuse;
- Docker host-gateway differences;
- KVM permissions and proof, HVF limitations, and TCG performance;
- capture, PCAPNG, packet history, retention, GUI, token, and controls;
- ports, volumes, files, cleanup, failures, and troubleshooting;
- inspection-only versus actual runtime validation;
- deferred TAP, broadcast/multicast-through-guest, physical SDR, and arbitrary external-node work.

### 14. Regression and quality gates

Run every feasible project check, including:

- clean CMake configure/build and complete CTest;
- targeted Phase 12 tests repeatedly;
- configured sanitizers and fuzzing;
- explicit versioned formatting and static-analysis tools required by the repository;
- telemetry unit/security/integration tests;
- GUI tests and production build;
- package installation and external consumer build;
- standard demo portable acceptance;
- Phase 11 UDP examples and tests;
- Compose validation and affected image builds;
- global organizational certificate/install-script paths for affected builds;
- real capture inspection with tshark/Wireshark tooling;
- profile lifecycle tests from a clean state.

Do not convert warnings, unavailable privileged features, or environmental failures into passing results. Investigate whether each is an implementation defect, documentation defect, pre-existing issue, or environment restriction.

## Security and resource review

Specifically assess:

- `/dev/kvm` access and dynamic group mapping;
- container user and Linux capabilities;
- absence of Docker socket and unjustified host mounts;
- QMP socket permissions and exposure;
- port binding and cross-container isolation;
- image provenance and organizational trust injection;
- guest input as hostile network input;
- packet length validation before allocation;
- payload logging/history privacy;
- capture path and download races;
- database and filesystem exhaustion;
- restart and malformed-input log amplification;
- control-token storage, transport, scope, and expiry.

## Finding severity

- **P0:** Immediate security, host compromise, destructive cleanup, data-loss, or catastrophic correctness issue.
- **P1:** A core acceptance requirement is unmet; protocol/deployment claims are false; lifecycle is unsafe; storage is unbounded; or the default container requires excessive privilege without justification.
- **P2:** Significant reliability, compatibility, observation, GUI, portability, security-hardening, operability, or maintainability defect.
- **P3:** Non-blocking improvement.

Every finding must include severity, acceptance ID, profile/platform, exact path/location, reproduction or evidence, expected and actual behavior, impact, remediation, and required regression test.

## Verdict rules

Return exactly one overall verdict:

- **ACCEPTED:** QEMU-001 through QEMU-017 pass, both profiles have their required runtime evidence, and only optional P3 findings remain.
- **CHANGES REQUIRED:** One or more implementation defects, P1/P2 findings, or acceptance failures remain.
- **REJECTED:** The implementation is unsafe, fundamentally misrepresents raw traffic/deployment, substantially duplicates rather than unifies the profiles, or fails to provide the two requested demos.
- **BLOCKED:** External restrictions prevent enough required runtime verification to reach a responsible verdict after all safe in-scope alternatives are exhausted.

If verification is performed only on macOS, the Linux container KVM requirement cannot be accepted. If verification is performed only on Linux, external Linux runtime can validate much of the portable profile, but macOS-specific claims must remain explicitly unverified unless supported by separate recorded evidence. Use `BLOCKED` only when missing environment prevents an overall verdict; use `CHANGES REQUIRED` when implementation defects also exist.

## Report format

Write `phase_12_verification.md` with:

1. Verdict.
2. Executive summary.
3. Environment, repository state, and change audit.
4. QEMU-001 through QEMU-017 matrix with implementation evidence, runtime evidence, profile/platform, status, and remediation.
5. Shared-versus-profile-specific implementation assessment.
6. Findings ordered by severity.
7. Exact commands, results, counts, durations, and artifact hashes.
8. External-profile runtime results by platform and accelerator.
9. Container TCG and KVM runtime results.
10. Raw protocol, topology, lifecycle, observation, capture, history, GUI, control, security, and resource assessments.
11. Regression and compatibility results.
12. Runtime-verified, simulated, inspection-only, failed, and unverified areas.
13. Required remediation and regression tests.
14. Residual risks and readiness recommendation for later TAP/L2 and SDR work.

Do not recommend beginning later QEMU TAP/L2 or SDR integration until Phase 12 is accepted or the remaining limitations are explicitly accepted by the project owner.
