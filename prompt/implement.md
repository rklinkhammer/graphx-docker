# GraphX implementation work package

Implement **Phase 14: static-route and deny-policy laboratory** in
`~/workspace/graphx-docker`. The roadmap source is
`prompt/architectural_roadmap_implementation_plan.md`, section 7.

## Objective

Deliver one small native-Linux teaching laboratory with three isolated IPv4
domains and three diagnostic flows:

```text
left -> middle   allowed and observable
middle -> left   denied by an ordered forwarding policy
left -> right    initially no route; succeeds only after route apply
```

The topology must make endpoint routes, the namespace router, OVS switches,
ordered policies, observation points, and `edge_paths` explicit. It must
distinguish a policy denial from a missing route and from an application error.

## Working rules

- Inspect repository instructions, status, Phase 13, existing native network
  labs, schema/configuration, infrastructure planner, telemetry, GUI, tests,
  documentation, and this contract before editing. Preserve unrelated changes.
- Do not commit, push, publish, deploy, attach a physical interface, or mutate
  unrelated host routes, firewall rules, namespaces, links, networks, or OVS.
- Keep configuration version 1 unless the existing model is demonstrably
  insufficient. Prefer the current router route/policy and ordered edge-path
  fields over a new public schema.
- Use only fixed `gx-route-*` laboratory names and private lab subnets that do
  not overlap checked-in examples.
- Every command, packet, wait, retry, log, artifact, capture, and cleanup action
  must be bounded. Native mutation requires an explicit Linux/sudo/tool gate.
- Portable validation and dry-run are inspection evidence only. They cannot
  prove OVS, namespaces, kernel routes, nftables counters, or packet outcomes.

## Required implementation

Create `examples/static-route-policy` with an authoritative `graphx.yaml`,
portable dry-run/configuration inspection, diagnostic endpoint code, and a
native-Linux lifecycle exposing:

```text
scripts/demo.sh start
scripts/demo.sh status
scripts/demo.sh verify
scripts/demo.sh apply-route
scripts/demo.sh clear-route
scripts/demo.sh logs
scripts/demo.sh stop
```

The lab must use three OVS-backed address domains, one disposable Linux
namespace router with three explicit interfaces, three disposable endpoint
namespaces, an OVS mirror for each domain, ordered allow/deny policies, and
ordered paths for all diagnostic edges. `start` must leave the selected static
route absent and prove the initial three-state baseline. `apply-route` must add
only the declared route to the declared endpoint and make only the missing-route
flow succeed. `clear-route` must restore the initial state.

Use deterministic bounded UDP diagnostic datagrams and endpoint receipt logs so
one-way delivery can be proven without return traffic confusing directional
deny rules. Use packet capture and nftables counters as independent evidence.
Retain bounded PCAP evidence after teardown. Do not represent these packets as
GraphX envelopes or application-message history.

Lifecycle scripts must preflight Linux, required tools, sudo, fixed names,
subnet/address conflicts, and the GraphX CLI before mutation. They must record
ownership, reject stale/forged state and unowned collisions, roll back partial
startup and interruption, allow repeated stop, and prove exact cleanup. Cleanup
must delete only resources owned by the active run and must preserve unrelated
host state.

Expose diagnostic state to the existing GUI without inventing application
failures. The network view must show the three ordered paths and distinct
`allowed`, `policy-denied`, and `missing-route` classifications, followed by a
`route-applied` transition for the third flow. Reuse existing telemetry and GUI
contracts where possible; any extension must be bounded, allow-listed, tested,
and backward compatible.

Add configuration/schema/planner/script/diagnostic tests, dry-run golden
assertions, lifecycle/static safety checks, telemetry/GUI tests, QEMU/SDR and
existing network-example regressions, and a manual native-Linux acceptance gate.

Update the architecture source and DOCX, example indexes, network and graphical
guides, test procedure/reference, changelog, and support/compatibility text as
needed. Regenerate authoritative projections only if their source changes.

## Acceptance identifiers

- **ROUTE-001 Model:** three domains, router interfaces, static route, ordered
  policies, OVS observation points, and complete ordered edge paths validate.
- **ROUTE-002 Realization:** native Linux state matches the declaration across
  namespaces, links, OVS, addresses, forwarding, nftables, and capture points.
- **ROUTE-003 Static-route transition:** the declared flow fails specifically
  for missing route, succeeds after apply, and fails again after clear.
- **ROUTE-004 Deny policy:** the denied flow never reaches its receiver, the
  matching nftables counter advances, and allowed flows remain unaffected.
- **ROUTE-005 Observation and GUI:** logs, packet evidence, telemetry, and GUI
  show honest distinct classifications and ordered paths.
- **ROUTE-006 Idempotence:** two lifecycle cycles, rollback, interruption,
  stale state, repeated stop, and route apply/clear are deterministic.
- **ROUTE-007 Host isolation:** unrelated routes, rules, namespaces, links,
  Docker networks, processes, and OVS state are unchanged.
- **ROUTE-008 Bounds:** datagrams, files, capture, logs, waits, retries, state,
  diagnostics, and cleanup are bounded and validated.
- **ROUTE-009 Compatibility:** config v1, existing transports, Phase 13 SDR,
  QEMU, network labs, telemetry, GUI, packaging, and quality gates still pass.
- **ROUTE-010 Documentation:** architecture and operator guides accurately cover
  intent, commands, state transitions, inspection, evidence, cleanup, security,
  limitations, and troubleshooting.

## Required evidence and exit

Run feasible non-destructive builds, CTest, schema/configuration, projection,
Compose if used, telemetry/web, format/static, sanitizer/fuzz gates proportional
to changed code, and all portable dry-run tests. On native Linux, run two full
start/baseline/apply/verify/clear/stop cycles and the adversarial lifecycle
matrix, comparing recorded host state before and after.

Write `phase_14_handoff.md` with the requirement matrix, changed paths, exact
commands/results, platform and repository state, evidence classification,
native-Linux operator steps, and remaining limitations. Phase 14 cannot be
claimed complete from macOS or dry-run evidence alone.
