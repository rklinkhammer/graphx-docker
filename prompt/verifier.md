# GraphX independent verification work package

Independently verify **Phase 13: single-SDR simulated and external profiles** in
`~/workspace/graphx-docker` against `prompt/implement.md`, the repository, and
acceptance identifiers SDR-001 through SDR-012. Read `phase_13_handoff.md` only
as a claim. Write the result to `phase_13_verification.md`.

## Independence and evidence

- Read the complete contracts, repository instructions, baseline history,
  status/diff, schema/configuration, implementation, docs, and tests.
- Do not make product fixes, discard changes, commit, push, publish, deploy,
  attach a physical interface, or trust filenames/log labels as proof.
- Derive the matrix independently and map every status to direct implementation
  evidence plus required runtime evidence.
- Classify checks as portable runtime, native-Linux runtime, automated simulated
  dependency, inspection only, blocked by environment, not applicable, or failed.
- A portable bridge does not prove OVS/SPAN. A namespace simulator does not prove
  physical hardware. Inspection does not prove runtime behavior.

## Verification procedure

### 1. Baseline and reuse

Record OS/kernel/architecture, Docker/Compose, OVS, iproute2, tcpdump/TShark,
OpenSSL, Python, compiler/CMake/Node, privileges, and repository state. Run the
existing portable acceptance baseline. Trace both profiles to one protocol,
simulator, processor, sink, controller, observer, telemetry service, GUI, and
packet-history format. Check QEMU observer defaults for regression.

### 2. Configuration and topology

Validate/project both configurations and compare normalized nodes/edges. Confirm
external lifecycle and switch/path metadata. Adversarially test unknown runtime,
bad ports/directions/schema, raw framing without external data-plane, attempts to
construct raw edges, a GraphX-managed cycle, and an external data/control cycle.
The first cycle must fail and the second must not weaken GraphX scheduling rules.

### 3. Wire and endpoint behavior

Compare deterministic UDP bytes at sender, capture, and processor. Exercise
minimum/maximum sample counts, truncation, bad magic/count/length, UDP loss,
duplicates/reordering, wrong endpoint, and restart. Prove result bytes reach the
distinct sink and contain bounded validated fields. Confirm no `GXE` envelope or
`u32be` GraphX length prefix is required.

### 4. Control and security

Prove TLS 1.3, CA validation, server-name verification, required client
certificate, `start/stop/tune/status`, bounds, invalid JSON/framing/action/range,
expired/untrusted/missing certificate, and replay-independent connections.
Confirm GUI pause/resume reaches only the processor controller and causes
accurate SDR transitions; Reset must retain its documented collector-only
meaning. Inspect private-key modes and default listeners.

### 5. Portable profile

Run the documented start/status/verify/logs/token/control/stop workflow on each
available portable host, twice. Verify live API and WebSocket changes without
refresh, every GUI tab transition, accurate bridge-simulation labeling, capture
catalog/download, Ethernet link type, packet-history query/pagination/separation,
disable flags, bounded artifacts, and stopped cleanup.

Inject occupied GUI/network resources, missing tools, invalid credentials,
container failure, observer/capture failure, interrupted start, repeated stop,
and stale state. Confirm actionable nonzero exits and no unrelated mutation.

### 6. Native-Linux OVS/SPAN profile

On native Linux, run the documented workflow and independently inspect OVS
bridge/ports/mirror, macvlan parent, namespace address/MAC, routes, listeners,
packet flow, SPAN frames, container isolation, process ownership, and GUI
hierarchy. Stop/restart twice and prove all fixed lab interfaces, namespace,
bridge, network, listeners, and processes are removed while evidence remains.

Interrupt each startup stage and test name collisions, inaccessible GraphX CLI,
missing OVS/tcpdump, stale/forged PID state, and capture limit. Confirm cleanup
never broadly matches processes and never touches a physical interface.

### 7. Capture, history, GUI, and compatibility

Validate classic PCAP tailing under partial records/replacement/truncation,
Ethernet PCAPNG with an independent reader, attribution rules for all three
edges, unknown packet honesty, database time/count/byte bounds, safe capture
paths/symlinks, and shutdown flush. Distinguish `network_packet` telemetry from
GraphX message history. Run unit/CTest, schema, configuration, telemetry/web,
format/static/sanitizer gates feasible on the host plus QEMU static regressions.

Review architecture, ADR index/record, examples index, shared/profile guides,
physical-device contract, and test procedure for accurate commands and limits.

## Acceptance matrix and verdict

Report SDR-001 through SDR-012 with requirement, implementation path/evidence,
validation command/evidence, status (`Implemented`, `Partial`, `Missing`, or
`Not Yet Applicable`), and precise remediation. Also report commands/results,
architectural drift, stale docs, missing tests, security/resource risks, and
environmental limitations.

Phase 13 passes only when portable simulation passes on macOS and Linux and the
OVS/SPAN profile passes on native Linux. Physical hardware is optional if its
attachment/ownership contract and simulated wire behavior pass. If native Linux
was not run, the overall result is incomplete rather than passed.
