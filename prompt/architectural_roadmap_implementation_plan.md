# GraphX architectural roadmap implementation and verification plan

**Repository:** `~/workspace/graphx-docker`  
**Roadmap source:** `docs/GraphX_Architecture.md`, section 13  
**Baseline:** GraphX 1.0.0 with Phases 3–12 accepted and the documentation and configuration consistency foundation implemented  
**Plan status:** Proposed for review  
**Proposed phase range:** 13 through 21

## 1. Objective

Execute the remaining recommendations in section 13 as a sequence of bounded,
independently verifiable work packages. The sequence must add useful examples
before broad platform machinery, preserve configuration and protocol
compatibility, and require explicit architecture decisions before changing
public contracts.

The plan covers:

1. a single-SDR example in simulated and external profiles;
2. a static-route and deny-policy network laboratory;
3. an application/Ethernet dual-capture correlation prototype;
4. a decision and implementation boundary for one-to-many logical edges;
5. infrastructure ownership, reconciliation, rollback, and recovery semantics;
6. capture rotation, indexing, retention, and correlation contracts;
7. a QEMU TAP/OVS and physical-node attachment security boundary;
8. conditional IPv6 and authenticated UDP/DTLS evaluation; and
9. conditional distributed telemetry/control state.

The phase numbers below are proposed. Approving this master plan does not by
itself approve every later or conditional capability. Each phase requires a
dedicated `prompt/implement.md`, `prompt/verifier.md`, ADR updates where stated,
an implementer handoff, and an independent verification report.

## 2. Sequencing principles

- Keep `graphx.yaml` authoritative. Checked-in projections are generated with
  `graphx project` and remain non-authoritative.
- Prefer examples that exercise existing contracts before changing those
  contracts.
- Keep external raw Ethernet traffic distinct from GraphX envelopes and message
  history.
- Treat application PCAPNG and Ethernet PCAPNG as separate evidence sources;
  correlation may join them but must not rewrite either source.
- Keep host-network mutation behind explicit Linux-only privilege gates and
  idempotent lifecycle scripts.
- Do not add config v2, IPv6, DTLS, TAP, physical attachment, or distributed
  state without an accepted compatibility/security ADR.
- Preserve macOS portable behavior while stating when native Linux is the only
  semantic acceptance environment.
- Keep every queue, file, capture, index, query, payload, diagnostic, retry,
  readiness wait, shutdown, and cleanup operation bounded.
- Implementation evidence and independent verification evidence are separate.
  A handoff, dry run, code inspection, or simulated dependency is not a runtime
  pass for native networking, physical hardware, KVM, or failure recovery.

## 3. Roadmap overview

| Phase | Work package | Priority | Principal dependency | Decision gate |
|---:|---|---|---|---|
| 13 | Single SDR simulated and external profiles | Immediate | Phase 12 raw-node, GUI, packet capture/history | External attachment and TCP-control trust boundary ADR |
| 14 | Static routes and deny-policy laboratory | Immediate | Existing network model and native Linux labs | None if configuration v1 remains unchanged |
| 15 | Dual-capture correlation prototype | Near term | Phase 9 capture and Phase 14 observation paths | Correlation evidence contract ADR |
| 16 | One-to-many logical-edge decision | Near term | UDP multicast experience and Phase 13 SDR fan-out needs | Config v1 extension versus config v2 ADR |
| 17 | Infrastructure ownership and reconciliation | Platform foundation | Existing `graphx infra` laboratories | Ownership/reconcile/rollback ADR |
| 18 | Capture lifecycle and correlation service | Platform foundation | Phase 15 prototype and Phase 17 ownership semantics | Rotation/index/retention ADR |
| 19 | QEMU TAP/OVS and physical attachment boundary | Later | Phase 17 lifecycle foundation | Security/lifecycle ADR and explicit operator opt-in |
| 20 | IPv6 and authenticated UDP/DTLS evaluation | Conditional | Stable transport/config compatibility policy | Threat model and compatibility decision |
| 21 | Distributed telemetry/control state | Conditional | Demonstrated multi-collector availability requirement | Consistency, identity, and failover ADR |

## 4. Common deliverables for every implementation phase

Each implementation phase must produce:

1. a phase-specific implementation contract in `prompt/implement.md`;
2. a matching independent verifier contract in `prompt/verifier.md`;
3. accepted or superseded ADRs for consequential architectural decisions;
4. implementation, configuration, schema, tests, examples, and documentation
   required by the phase—never documentation-only claims for runtime behavior;
5. deterministic lifecycle commands with `start`, `status`, `verify`, `logs`,
   and `stop` where a runnable demonstration is introduced;
6. a `<phase>_handoff.md` recording exact commands, results, skips, platform,
   commit/worktree state, and remaining limitations;
7. regenerated configuration projections when authoritative configurations
   change;
8. an updated architecture source and regenerated, visually checked DOCX when
   the architecture changes; and
9. changelog and support/compatibility updates for user-visible behavior.

The independent verifier writes `<phase>_verification.md`, derives its own
traceability matrix, and must not treat the handoff as proof.

## 5. Common acceptance gates

Unless a phase contract explicitly justifies a narrower set, the implementer
and verifier run:

- `scripts/verify.sh quick`;
- `scripts/verify.sh portable`;
- LLVM 21 formatting and static analysis;
- sanitizers and bounded fuzzing when native C++ parsing, protocol, lifecycle,
  capture, or transport code changes;
- Docker/Compose validation and affected image builds;
- configuration validation for root and every example;
- `graphx project --check` for root projections;
- package/release contract tests when installed files or public CLI/API behavior
  changes;
- native Linux acceptance for macvlan, ipvlan, OVS, namespaces, TAP, netem,
  nftables, dumpcap, KVM, or special-file/permission behavior; and
- macOS portable acceptance for every feature claimed to be portable.

Every report classifies evidence as runtime verified, automated simulation,
inspection only, not applicable, blocked by environment, or failed. A skipped
native gate cannot be converted into a pass.

## 6. Phase 13 — Single SDR simulated and external profiles

### Goal

Add the smallest useful SDR topology while reusing Phase 12 external-node,
packet-observation, GUI, history, and capture patterns:

```text
SDR -- raw UDP sample blocks --> processor -- result edge --> sink
SDR <-- authenticated TLS/TCP control ---------------- processor
```

The user-facing topology should contain one SDR, one Ethernet switch, one
processor, and one sink. The processor also acts as the controller so the TCP
control path does not require another visible application node.

### Implementer scope

- Add `examples/sdr-node/common`, `simulated`, and `external` profiles.
- Implement one bounded simulated SDR endpoint reused by the portable demo and
  verifier. It emits deterministic UDP sample blocks and accepts a narrow TCP
  control vocabulary such as start, stop, tune, and status.
- Model SDR data/control edges as `data_plane: external` with `framing: none`;
  do not instantiate them through `TransportFactory` or label packets as GraphX
  messages.
- Place the SDR and processor on an explicit Ethernet path containing one OVS
  switch and SPAN output in the native Linux profile. Provide a clearly labeled
  portable bridge/simulation profile for macOS.
- Provide a physical/external SDR configuration contract with explicit address,
  interface, port, ownership, and operator opt-in. GraphX must not reconfigure or
  stop hardware it does not own unless a future capability explicitly says so.
- Reuse packet telemetry, bounded packet history, Ethernet PCAPNG, capture
  catalog/download, and the current GUI. Show the SDR as external hardware, the
  switch as infrastructure, and the processor/sink as managed services.
- Authenticate TCP control or bind it to an isolated local test network with an
  explicit threat-model decision. Do not imply UDP confidentiality, integrity,
  reliability, or authorization.
- Enable bounded capture and packet history by default with explicit disable
  flags, matching the QEMU demo experience.
- Add step-by-step operation, packet inspection, GUI, control, cleanup, and
  troubleshooting documentation.

### Verifier focus

- Prove deterministic UDP payload bytes reach the processor and results reach
  the sink; separately prove TCP control state transitions and rejection of
  malformed/unauthorized commands.
- Confirm data and control use ordinary Ethernet/IP traffic, not GraphX framing.
- Confirm the simulated and external profiles share endpoint/payload/observer/UI
  code and differ only at the deployment/attachment boundary.
- Verify live GUI updates, accurate external-node and switch hierarchy, packet
  history separation, bounded Ethernet capture, and all tab transitions.
- On native Linux, independently inspect OVS ports/SPAN, interface/address
  isolation, listeners, packet capture, cleanup, and repeated start/stop.
- Confirm the external profile never broadly kills, reconfigures, or claims
  ownership of a physical SDR.

### Acceptance identifiers

`SDR-001` topology; `SDR-002` raw UDP integrity; `SDR-003` TCP control boundary;
`SDR-004` simulated/external reuse; `SDR-005` OVS/SPAN path; `SDR-006` live
observation; `SDR-007` capture/history; `SDR-008` GUI; `SDR-009` lifecycle and
cleanup; `SDR-010` security/bounds; `SDR-011` compatibility; `SDR-012`
documentation.

### Exit criteria

Portable simulation passes on macOS and Linux. Native OVS/SPAN and physical-link
semantics are accepted only from a native Linux run. A real SDR is optional for
Phase 13 acceptance if the external attachment contract and simulated wire
contract are fully verified; documentation must state that limitation.

## 7. Phase 14 — Static-route and deny-policy laboratory

### Goal

Create a teaching laboratory with three routed domains where:

1. one intended flow succeeds;
2. one flow is denied by policy; and
3. one flow initially fails and succeeds only after the documented static route
   is applied.

### Implementer scope

- Add one native Linux topology with three isolated address domains, explicit
  router interfaces, at least one static route, allow and deny policies, OVS
  observation points, and ordered `edge_paths`.
- Keep the graph small and deterministic. Use diagnostic endpoints rather than a
  second application architecture.
- Extend infrastructure planning only if existing route/policy fields cannot be
  applied, inspected, or removed correctly.
- Make create/destroy/status/dry-run idempotent and preserve unrelated host
  routes, namespaces, nftables rules, links, networks, and OVS state.
- Surface intended, denied, and missing-route states distinctly in logs and GUI;
  never label a policy denial as application failure.
- Add a portable configuration/dry-run view, clearly labeled as inspection rather
  than native behavior.

### Verifier focus

- Compare the declared path, generated infrastructure plan, and actual kernel,
  Docker, OVS, route, and nftables state.
- Prove the successful, denied, and missing-route outcomes with packet evidence,
  then apply the route and prove only the intended outcome changes.
- Inject duplicate/stale resources, partial startup, occupied names/subnets,
  invalid next hops, policy-order changes, and interrupted cleanup.
- Confirm no unrelated host route or firewall state is changed.
- Verify GUI path highlighting and diagnostic classification.

### Acceptance identifiers

`ROUTE-001` model; `ROUTE-002` realization; `ROUTE-003` static-route transition;
`ROUTE-004` deny policy; `ROUTE-005` observation/GUI; `ROUTE-006` idempotence;
`ROUTE-007` host isolation; `ROUTE-008` bounds; `ROUTE-009` compatibility;
`ROUTE-010` documentation.

### Exit criteria

The native Linux laboratory passes repeated create/verify/destroy cycles and
returns the host to its recorded baseline. Portable dry-run coverage is
supporting evidence only.

## 8. Phase 15 — Application and Ethernet dual-capture correlation prototype

### Goal

Correlate GraphX USER0 application frames with separately recorded Ethernet
frames without merging trust domains or modifying source captures.

### Implementer scope

- Run a GraphX-aware TCP pipeline across an observed OVS path and record both
  current application PCAPNG and Ethernet PCAPNG.
- Define an ADR for correlation inputs, clock assumptions, match confidence,
  ambiguity, redaction, resource bounds, and evidence provenance.
- Implement an offline, deterministic correlation tool producing a bounded JSON
  report and concise human-readable summary.
- Use message identity where available plus edge, endpoint, direction, frame
  length, and a configured timestamp window. Never claim certainty when several
  Ethernet frames are plausible.
- Preserve capture bytes and hashes. Record source filenames, link types,
  timestamps, tool version, and report-generation parameters.
- Add a GUI inspection view only after the offline contract is stable. The GUI
  must show correlated, ambiguous, and unmatched records distinctly.
- Keep payload previews disabled or bounded/redacted by default.

### Verifier focus

- Independently decode both captures, reproduce the report, and check every
  match against raw timestamps/lengths/identities.
- Exercise missing packets, duplicates, retransmission, clock skew, reordered
  records, truncated captures, different link types, malformed blocks, hash
  mismatch, oversized inputs, and path/symlink attacks.
- Confirm deterministic output, bounded memory/time/output, source immutability,
  and honest ambiguity handling.
- Verify GUI rendering does not convert correlation inference into protocol fact.

### Acceptance identifiers

`CORR-001` evidence contract; `CORR-002` deterministic tool; `CORR-003` matching;
`CORR-004` ambiguity; `CORR-005` provenance; `CORR-006` malformed-input safety;
`CORR-007` resource bounds; `CORR-008` GUI; `CORR-009` compatibility;
`CORR-010` documentation.

### Exit criteria

The prototype produces reproducible results for a checked-in bounded fixture and
a live native Linux OVS run. Rotation, durable indexing, and continuous service
operation remain Phase 18 work.

## 9. Phase 16 — One-to-many logical-edge decision

### Goal

Decide whether native fan-out belongs in a backward-compatible configuration v1
extension or requires configuration v2, then implement only the accepted scope.

### Decision work before code

The ADR must compare at least:

1. multiple existing directed edges sharing one source port;
2. one edge with multiple targets;
3. a multicast/fan-out node represented explicitly in the graph; and
4. a configuration v2 edge contract.

Evaluate schema compatibility, edge identity, per-target delivery and metrics,
UDP multicast membership, backpressure, partial failure, control, GUI rendering,
history, capture correlation, deployment, and upgrade/downgrade behavior.

### Implementer scope

- First add characterization examples using existing multiple-edge semantics
  where valid; do not change the schema merely for visual convenience.
- Write the compatibility ADR and migration plan with explicit owner approval.
- If v1 extension is selected, require old readers to reject or safely ignore the
  addition according to the documented compatibility policy.
- If v2 is selected, implement a versioned loader/schema, deterministic
  inspection/projections, explicit conversion guidance, and mixed-version tests.
- Model per-target status and counters; do not hide partial delivery behind one
  aggregate success state.
- Use the routed multicast receiver concept as an acceptance example only after
  the logical semantics are defined.

### Verifier focus

- Challenge compatibility claims with old and new configurations/readers.
- Verify graph validation, cycle detection, endpoint direction, duplicate target
  handling, per-target metrics/history/capture, GUI selection, and partial
  failure semantics.
- Prove unicast, broadcast, multicast, and non-UDP transports are not conflated.
- Verify downgrade and rejection behavior from packaged 1.0.0 artifacts.

### Acceptance identifiers

`FANOUT-001` decision completeness; `FANOUT-002` compatibility; `FANOUT-003`
validation; `FANOUT-004` runtime semantics; `FANOUT-005` partial failure;
`FANOUT-006` observability; `FANOUT-007` GUI; `FANOUT-008` migration;
`FANOUT-009` regression; `FANOUT-010` documentation.

### Stop/go criterion

No schema or runtime change begins until the ADR is accepted. If existing
multiple-edge semantics satisfy the use cases, close the phase with that
decision and examples rather than creating config v2 unnecessarily.

## 10. Phase 17 — Infrastructure ownership, reconciliation, and rollback

### Goal

Move `graphx infra` from laboratory-oriented create/destroy plans toward an
explicit ownership and recovery contract before TAP or broader physical-network
integration.

### Implementer scope

- Define an ADR covering desired versus observed state, ownership identity,
  persisted state, adoption, conflicts, drift classes, rollback, crash recovery,
  locking, concurrent invocations, and versioning.
- Introduce a bounded state/ownership manifest with atomic update and restrictive
  permissions. Never infer ownership from names alone.
- Add plan/apply/status/reconcile/destroy semantics with machine-readable plans
  and dry-run parity.
- Preflight all operations, record reversible actions, and roll back only
  resources proven to be owned by the invocation/project.
- Refuse ambiguous pre-existing resources by default; allow adoption only through
  an explicit reviewed option.
- Cover Docker networks, host links, namespaces, OVS bridges/ports/mirrors,
  routes, nftables policies, and netem state already supported by the model.
- Make cleanup restartable after process death and retain bounded audit evidence.

### Verifier focus

- Create drift and conflicts independently across every resource class.
- Interrupt application at each mutation boundary and verify recovery/rollback.
- Run concurrent invocations, stale-lock cases, tampered manifests, partial state,
  version mismatch, permission failure, and manual resource edits.
- Confirm GraphX never deletes or rewrites an unowned resource and does not claim
  convergence when observed state differs.
- Verify repeated reconcile/no-op behavior and exact cleanup to baseline.

### Acceptance identifiers

`INFRA-001` ownership; `INFRA-002` observed-state model; `INFRA-003` plan/apply;
`INFRA-004` reconciliation; `INFRA-005` rollback; `INFRA-006` crash recovery;
`INFRA-007` concurrency; `INFRA-008` unowned-resource safety; `INFRA-009` audit
and bounds; `INFRA-010` compatibility/documentation.

### Exit criteria

Native Linux failure-injection testing demonstrates safe convergence or an
explicit actionable refusal for all supported resource classes. This phase is a
prerequisite for Phase 19.

## 11. Phase 18 — Capture rotation, indexing, retention, and correlation service

### Goal

Turn the Phase 15 offline prototype and existing bounded captures into a durable,
operator-managed evidence lifecycle without making capture required for graph
execution.

### Implementer scope

- Define capture segment identity, rotation triggers, atomic finalization,
  catalog/index schema, retention ordering, deletion ownership, recovery, and
  correlation provenance in an ADR.
- Support bounded size/time/packet rotation with finalized immutable segments.
- Build a separate bounded index that can be rebuilt from trusted capture
  metadata and does not treat unvalidated paths or packet bytes as SQL/query
  structure.
- Apply age/count/byte retention without deleting open, foreign, hard-linked, or
  unowned files. Make retention failure degrade capture health, not graph traffic.
- Extend correlation across segments while preserving ambiguous/unmatched states
  and source hashes.
- Expose bounded catalog, query, download, retention, and correlation status in
  the API/GUI with observation authorization separate from control.
- Add upgrade/recovery behavior for existing single-file captures.

### Verifier focus

- Test exact rotation boundaries, concurrent readers, crash during finalization,
  disk full, permissions, index corruption, rebuild, clock changes, retention
  races, symlinks, hardlinks, FIFOs, sparse/oversized files, and foreign files.
- Confirm capture failure never blocks or crashes graph processing.
- Verify all API pagination/query limits, authorization, redaction, download
  descriptor safety, and GUI behavior across segment changes.
- Independently compare index records and correlations to capture bytes.

### Acceptance identifiers

`CAPLIFE-001` segment contract; `CAPLIFE-002` rotation; `CAPLIFE-003` finalization;
`CAPLIFE-004` index; `CAPLIFE-005` retention; `CAPLIFE-006` recovery;
`CAPLIFE-007` cross-segment correlation; `CAPLIFE-008` API/GUI;
`CAPLIFE-009` security/bounds; `CAPLIFE-010` compatibility/documentation.

## 12. Phase 19 — QEMU TAP/OVS and physical-node attachment boundary

### Goal

Add an opt-in native Linux QEMU TAP profile and define a reusable boundary for
attaching VMs or physical Ethernet nodes without weakening default deployments.

### Required decision before implementation

Accept a security/lifecycle ADR defining privilege location, TAP ownership,
interface/bridge attachment, VLAN policy, MAC/IP trust, DHCP/static addressing,
host firewall interaction, capture exposure, teardown, and recovery. Phase 17
ownership semantics must already be accepted.

### Implementer scope

- Add a Linux-only TAP/OVS QEMU profile sharing the existing Phase 12 guest and
  endpoint code.
- Use the least privilege that can create/attach the required TAP resources;
  keep QEMU itself non-root where practical.
- Never make TAP, host networking, `NET_ADMIN`, `/dev/net/tun`, or privileged
  containers a requirement for the existing slirp profiles.
- Model guest MAC identity, OVS port/VLAN path, SPAN capture, and L2
  multicast/broadcast accurately in configuration and GUI.
- Use Phase 17 ownership/reconcile for every host resource.
- Define but do not automatically mutate a physical NIC attachment unless the
  operator supplies an explicit interface and confirms the risk boundary.

### Verifier focus

- Audit actual capabilities, users/groups, devices, namespaces, listeners, OVS
  state, firewall state, and cleanup.
- Prove guest L2 identity, VLAN isolation, multicast/broadcast, unicast, SPAN,
  restart, failure rollback, and exact host restoration.
- Attempt interface-name substitution, unowned bridge/TAP collisions, spoofed
  MAC/IP, VLAN escape, broad host exposure, and interrupted teardown.
- Confirm old external/container slirp demos remain unprivileged and unchanged.

### Acceptance identifiers

`TAP-001` security model; `TAP-002` least privilege; `TAP-003` ownership;
`TAP-004` L2 identity; `TAP-005` VLAN/isolation; `TAP-006` multicast/broadcast;
`TAP-007` SPAN/capture; `TAP-008` lifecycle/recovery; `TAP-009` slirp
compatibility; `TAP-010` physical-boundary documentation.

## 13. Phase 20 — Conditional IPv6 and authenticated UDP/DTLS evaluation

### Goal

Determine whether IPv6 and authenticated UDP are justified, compatible, and
operable. This phase begins as research/design and may end without runtime
implementation.

### Decision work

- Define concrete use cases and threats. Separate IPv6 addressing/routing from
  UDP authentication, confidentiality, replay defense, multicast, and key
  management.
- Compare DTLS with application-layer authentication and deployment-level
  network protection.
- Analyze socket/API/config/schema changes, dual-stack ambiguity, PMTU,
  fragmentation, multicast scope/interface selection, DNS, capture/dissection,
  metrics cardinality, and migration.
- Decide config v1 extension versus config v2 consistently with Phase 16.
- Record supported cipher/version/certificate or PSK lifecycle only if a secure
  implementable profile is selected.

### Implementation only after go decision

- Add the smallest bounded unicast example first.
- Add IPv6 multicast or DTLS only as separately testable increments.
- Reuse the project certificate/trust mechanism without embedding secrets.
- Add protocol, parser, fuzz, sanitizer, interop, capture, Wireshark, GUI, and
  failure-path tests.

### Verifier focus

- Verify the threat model and compatibility decision before testing code.
- Exercise dual-stack selection, malformed addresses, scope IDs, MTU boundaries,
  certificate/PSK failure, replay, expiry/rotation, downgrade, packet loss,
  reordering, fragmentation, shutdown, and bounded resource use as applicable.
- Require packet-level evidence and independent interoperability tools for any
  wire-protocol claim.

### Stop/go criterion

Do not implement both IPv6 and DTLS merely because they share UDP code. Split
them into later phases if either decision lacks a concrete accepted requirement.

## 14. Phase 21 — Conditional distributed telemetry and control state

### Goal

Define distributed state only after a demonstrated requirement for multi-
collector availability. This is not committed functionality in the current
roadmap.

### Entry criteria

- A documented availability target cannot be met by restart, durable local
  history, or an external load balancer.
- Operators accept the additional consistency, identity, certificate, upgrade,
  and operational complexity.
- Failure domains and authoritative ownership of commands, idempotency keys,
  audit records, history, and live observations are explicit.

### Decision work

- Compare active/passive, shared durable store, replicated log, and partitioned
  collectors.
- Define consistency and failover semantics for commands separately from
  best-effort observations.
- Specify leader/lease/fencing behavior, duplicate delivery, ordering, clock
  assumptions, credential distribution, audit provenance, split brain, schema
  migration, and disaster recovery.
- Preserve bounded queues and admission control during partitions.

### Implementation only after go decision

- Start with collector failover for observation ingestion while retaining one
  fenced control authority.
- Add distributed control only after independent failure-injection evidence for
  identity, idempotency, audit, and fencing.
- Keep single-collector mode supported and simpler.

### Verifier focus

- Independently inject partitions, process death, delayed/duplicated messages,
  clock skew, stale leaders, store failure, credential rotation, schema mismatch,
  and recovery.
- Prove no command can be accepted by two unfenced authorities and every terminal
  result has auditable provenance.
- Verify observation loss/duplication semantics are accurately reported rather
  than hidden.

### Stop/go criterion

If no measured multi-collector availability requirement exists, retain this as
an ADR-ready design note and do not add distributed runtime code.

## 15. Cross-phase dependency and release gates

```text
Phase 13 SDR example ───────────────┐
                                   ├─> Phase 16 fan-out decision
Phase 14 route/policy lab ─> Phase 15 correlation prototype ─> Phase 18 capture lifecycle
                │
                └─> Phase 17 infrastructure ownership ─> Phase 19 TAP/physical boundary

Phase 16 compatibility decision ──> Phase 20 IPv6/DTLS decision

Measured availability requirement ─> Phase 21 distributed state
```

Release gates:

- Phases 13–15 may remain backward-compatible 1.x example/tooling increments if
  they do not change existing public contracts.
- Phase 16 determines whether fan-out requires config v2 or another major
  compatibility boundary.
- Phase 17 must not silently change existing `graphx infra` destructive
  semantics; new reconcile/adoption behavior must be explicit and versioned.
- Phase 18 capture schema/API changes require migration and downgrade tests.
- Phases 19–21 require explicit opt-in and must not broaden default privilege,
  exposure, or operational complexity.

## 16. Prioritized execution recommendation

### Immediate

1. Phase 13 implementation and independent macOS/native-Linux verification are complete.
2. Phase 14 is implemented in the current candidate; execute
   its independent native-Linux verifier and remediate any findings.
3. Accept Phase 14 only after two complete native lifecycle cycles and the
   adverse-state/host-isolation matrix pass.

### Near term

1. Implement Phase 15 as an offline correlation prototype with immutable source
   captures.
2. Complete the Phase 16 decision before changing logical-edge cardinality.
3. Implement Phase 17 ownership/reconciliation before TAP or broader host
   attachment.
4. Promote proven Phase 15 behavior into Phase 18 rotation/index/retention.

### Later and conditional

1. Begin Phase 19 only after Phase 17 native Linux acceptance.
2. Begin Phase 20 only with approved IPv6 or authenticated-UDP requirements.
3. Begin Phase 21 only with a measured multi-collector availability requirement.

## 17. Definition of roadmap completion

The roadmap is complete only when every approved phase has:

- an accepted implementation contract and independent verifier contract;
- direct code/configuration evidence and proportionate runtime evidence;
- accepted ADRs and compatibility/migration decisions;
- passing portable and platform-specific regression gates;
- bounded lifecycle, storage, parsing, security, and failure behavior;
- updated architecture, examples, user documentation, projections, and release
  records; and
- an independent verification report whose current verdict is accepted or whose
  remaining limitations were explicitly accepted by the project owner.

Conditional Phases 20 and 21 may be closed with a documented no-go decision.
Roadmap completion does not require implementing capabilities for which the
entry criteria were never met.
