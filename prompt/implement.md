# GraphX implementation work package

Implement **Phase 13: single-SDR simulated and external profiles** in
`~/workspace/graphx-docker`. The roadmap source is
`prompt/architectural_roadmap_implementation_plan.md`, section 6.

## Objective

Deliver the same minimal logical topology in two profiles:

```text
SDR -- ordinary UDP IQ blocks --> processor -- ordinary TCP results --> sink
SDR <-- mutually authenticated TLS/TCP control --------------------- processor
```

The portable profile runs the SDR simulator and services in Docker on an
isolated bridge. The native-Linux profile models the SDR as external, connects a
disposable verifier endpoint through one OVS switch, and observes it through an
OVS SPAN port. Both profiles use one endpoint/payload/observer/GUI codebase.

## Working rules

- Inspect repository instructions, status, Phase 12 raw-node implementation,
  configuration/schema, telemetry, GUI, tests, documentation, and this contract
  before editing. Preserve unrelated changes.
- Do not commit, push, publish, deploy, contact external systems, or attach a
  physical interface.
- Do not label ordinary SDR packets as GraphX envelopes. External raw edges use
  `data_plane: external` and `framing: none` and never enter
  `TransportFactory`.
- Keep the GraphX-managed execution graph acyclic. If descriptive external
  data/control relationships form a cycle, make the exception explicit,
  narrowly validated, tested, and recorded in an ADR.
- Capture/history are bounded and on by default, with `--no-capture` and
  `--no-history` start options.
- All requests, payloads, queues, previews, files, retries, waits, logs, and
  cleanup are bounded. Default management exposure is loopback/private.
- GraphX does not own a physical SDR merely because it appears in topology.

## Required implementation

Create `examples/sdr-node/common`, `simulated`, and `external`. Shared code must
provide a deterministic bounded `SDR1` UDP IQ format, simulator, processor,
sink, direct controller, local TLS material generator, and generalized packet
attribution rules. The processor is the only visible controller node.

The control vocabulary is exactly `start`, `stop`, `tune`, and `status`. Use TLS
1.3 mutual certificate authentication, bounded JSON-line requests/responses,
peer-name verification, local short-lived demo credentials, and no embedded
private material. State plainly that UDP is unauthenticated, unreliable, and
unencrypted.

The simulated profile uses a private bridge and clearly identifies it as a
portable simulation. The external profile is native Linux only and uses a
macvlan Docker network, OVS bridge, disposable network namespace endpoint, and
SPAN capture interface. It must create/destroy only fixed lab resources, track
processes by validated PID and unique artifact path, roll back failed starts,
and support repeated start/stop. It must never adopt or mutate a real interface.

Both profiles expose a consistent CLI:

```text
demo.sh start [--no-capture] [--no-history]
demo.sh verify
demo.sh status
demo.sh logs
demo.sh token
demo.sh control status|start|stop|tune HERTZ
demo.sh stop
```

Reuse the existing telemetry/UI and raw packet observer. Generalize QEMU-specific
packet attribution without changing QEMU defaults. Emit `network_packet`
events, standard Ethernet PCAPNG, separate bounded SQLite packet history, live
edge/node metrics, capture catalog/downloads, and WebSocket GUI updates. GUI
control must relay only through the processor and must not imply physical-device
lifecycle ownership.

Add portable protocol/parser/TLS/attribution tests, both configuration gates,
schema/runtime compatibility tests, script and Compose validation, architecture
and ADR updates, common/profile guides, native-Linux operator gates, and a
Phase 13 handoff recording commands and evidence classification.

## Acceptance identifiers

- **SDR-001 Topology:** one SDR, Ethernet switch/path, processor/controller,
  sink, and accurate raw data/control/result edges in both profiles.
- **SDR-002 Raw UDP integrity:** deterministic bounded IQ bytes reach the
  processor without GraphX framing; malformed and mismatched packets are rejected.
- **SDR-003 TCP control boundary:** TLS 1.3 mTLS, peer verification, narrow
  commands, state transitions, and malformed/unauthorized rejection.
- **SDR-004 Reuse:** profiles share endpoint, payload, observer, telemetry, GUI,
  control semantics, evidence formats, and vocabulary. GUI pause/resume relay to
  the SDR; GUI Reset retains its existing collector-counter meaning.
- **SDR-005 OVS/SPAN:** native Linux proves ports, external endpoint, mirror,
  packet path, and exact teardown.
- **SDR-006 Live observation:** packet-derived counters advance over API and
  WebSocket without fabricated GraphX messages.
- **SDR-007 Capture/history:** default-on bounded Ethernet PCAPNG and separate
  packet history are valid, queryable, cataloged/downloadable, and disableable.
- **SDR-008 GUI:** topology/runtime ownership is accurate; live updates,
  controls, captures, history, and every tab transition work.
- **SDR-009 Lifecycle:** preflight, idempotence, bounded waits, rollback,
  validated process ownership, restart, and cleanup are demonstrated.
- **SDR-010 Security/bounds:** loopback/private exposure, protected local keys,
  no broad process kill, least privilege, bounded resources, and honest UDP risks.
- **SDR-011 Compatibility:** existing GraphX transports, QEMU behavior,
  configuration, tests, packaging, demos, and quality gates remain compatible.
- **SDR-012 Documentation:** shared/profile guides cover architecture,
  operation, GUI, control, inspection, physical ownership, cleanup, limitations,
  and troubleshooting.

## Required evidence and exit

Run feasible non-destructive portable builds/tests/config/schema/Compose/static
checks and the simulated profile where Docker is available. Add a manual native
Linux gate for OVS/SPAN. Classify evidence as runtime verified, automated
simulation, inspection only, blocked by environment, failed, or not applicable.
macOS cannot prove OVS/macvlan/netns behavior. A real SDR is optional; simulator
success must never be described as physical-hardware proof.

Write `phase_13_handoff.md` with the requirement matrix, changed paths, exact
commands/results, environmental limitations, remaining risks, and Linux steps.
