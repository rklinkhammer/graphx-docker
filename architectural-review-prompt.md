# Implementation design prompt for architectural review

Act as a systems architect reviewing and preparing the implementation of the
graph-driven deployment architecture described in [analysis.md](analysis.md).

Produce a concrete, reviewable implementation design and example package before
changing production behavior. The objective is to make new nodes and arbitrary
supported topologies require the least duplication of Compose, Dockerfiles,
network configuration, and launch scripts. Backward compatibility is not
required. Do not recreate a general-purpose deployment lifecycle framework.

## Authority and scope

Read `AGENTS.md`, `docs/project-decisions.md`, `analysis.md`,
`examples/README.md`, and `docs/test-procedure.md`. Inspect the current source,
schemas, Dockerfiles, Compose files, launchers, and tests before making claims.
Treat the repository as evidence of current behavior and `analysis.md` as the
requested future direction. Cite source paths for architectural findings.

The user has accepted the recommended resolutions of issues I-01 through I-11.
Do not reopen these decisions or request confirmation for them. Where a
resolution requires a concrete value or contract, select and document the
smallest consistent design. Record only newly discovered contradictions or
requirements as new issues, starting at I-12.

For this design exercise only, I-01 through I-11 are authorized future-state
premises. They take precedence over conflicting current-state decisions only
within clearly labeled proposed design artifacts. Current source, tests,
schemas, examples, and current-system documentation remain unchanged and
continue to describe version 2 and `graphx.yaml` until a separately authorized
implementation changes them. This exception does not relax execution permissions
or unrelated repository rules.

A new issue is a contradiction or missing requirement that prevents a single
consistent contract or acceptance test. For each, provide severity, affected
scenarios, source evidence, recommended resolution, and whether it blocks
implementation. Record ordinary implementation risks and selected concrete
values in their respective sections without new issue numbers.

This task authorizes design documents and illustrative fixtures, not production
implementation, infrastructure mutation, or privileged execution. Clearly label
proposed files as non-runtime design artifacts. Do not change the current-system
documentation to claim proposed functionality already exists. Provide the
implementation sequence for execution after the design review.

## Accepted decisions

1. **I-01: One breaking contract.** Use a single new configuration contract with
   `graphx.yml` as the canonical authored filename and version 3 as the design
   baseline. The C++ loader remains authoritative. Plan deliberate replacement
   of version 2, without a compatibility parser or a second manifest maintained
   by users. Align schema, normalized JSON, consumers, and documentation in the
   implementation plan.
2. **I-02: Explicit node binding.** Every managed application receives its node
   identity and resolved port bindings. Shared code and templates contain no
   sample-specific node or edge lookup assumptions.
3. **I-03: Explicit type capabilities.** Define schemas, transport support,
   connection cardinality, and feedback support per type. Validate fan-in and
   fan-out. Keep communication topology distinct from readiness dependencies.
4. **I-04: Explicit locality.** Preserve native examples. Shared-memory peers
   require a supported common IPC domain. Loopback retains its actual namespace
   meaning. Reject unsupported placements instead of silently changing transport.
5. **I-05: Explicit network realization.** Portable container connectivity and
   managed OVS attachments have distinct contracts. System OVS is the only
   managed data-plane backend; MACVLAN/IPVLAN remain semantic profiles. Resolve
   broadcast and multicast interfaces explicitly. Prevent management connectivity
   from bypassing the intended data plane.
6. **I-06: Derived names and checked endpoints.** Derive physical names and
   ordinary endpoints from graph identity and logical bindings. Preserve explicit
   laboratory addressing. Define bounded names and collision handling; distinguish
   compile-time conflicts from execution-time resource availability checks.
7. **I-07: Explicit guest ownership.** Separate managed QEMU execution from
   attaching an external guest. Specify guest artifacts, architecture,
   acceleration, configuration and credential delivery, and readiness. A guest
   must implement its declared application contract.
8. **I-08: Honest external-device modeling.** Lifecycle ownership is independent
   of raw versus GraphX encoding. Declare observation capabilities. External
   devices do not implicitly emit GraphX telemetry; laboratory simulators are
   separately declared test fixtures.
9. **I-09: Separate deployment and scenarios.** Keep timed faults, deferred routes,
   credential rotation, and verification traffic separate from baseline startup.
   Reuse graph resource references rather than duplicate network declarations in
   scenario scripts.
10. **I-10: Default platform with explicit access and storage.** Every graph gets
    web console, telemetry, and bounded persistent SQLite history, initially in
    one platform service. Define native-process telemetry access, console port
    selection and Lima forwarding, storage limits, and explicit history deletion.
    Privileged state and high-I/O artifacts remain on Linux under `/var/lib/graphx`.
11. **I-11: Bounded templates.** Use a small validated node/platform template
    contract. Do not expose arbitrary Compose overrides or introduce a second
    configuration interpreter. Add a type once and reuse it across graphs.

## Required architecture

- One graph compiler consumes the authored graph, a reusable type/template
  catalog, and an execution target. It emits resolved configuration, per-node
  settings, Compose, optional build recipes, and native/OVS/QEMU execution
  artifacts as needed.
- Compilation is deterministic for fixed inputs and performs no runtime
  mutation. Secret material is not embedded in graphs or normalized output.
  Define relative-path resolution, template/image version selection, and output
  ownership sufficiently to make generated artifacts reproducible and reviewable.
  Specify canonical node, connection, service, and artifact ordering and stable
  serialization; state where byte-for-byte or semantic determinism applies.
  Define path bases, allowed source/output/credential roots, and rejection of
  paths escaping those roots. Pin catalog and image/build-recipe identities.
  Define treatment of host ports, addresses, timestamps, random identifiers, and
  credential references, including an explicit list of values allowed to remain
  unresolved until execution. Specify overwrite and stale-output behavior for an
  existing output directory without bypassing ownership checks.
- Prefer shared existing images. Changing topology normally produces no new
  Dockerfile. New software can select a validated build recipe. Treat QEMU guest
  artifacts separately from container images.
- Keep `apps/` thin and reusable behavior in the existing public/source layers.
  Reuse existing identity-checked infrastructure operations. Generated plans are
  derived artifacts, not a parallel source of truth or a new lifecycle ledger.
- Compose manages containers and management connectivity. Native processes and
  QEMU need explicit execution artifacts. Explain the smallest execution glue
  needed to consume them, including startup ordering, readiness, and cleanup.
- On macOS, retain OrbStack for unprivileged containers and Lima for privileged
  Linux networking and QEMU. Preserve native process coverage. Never forward
  privileged sockets to macOS.
- Include credentials, authorization, bounded capture/history, and existing
  ownership guarantees without adding continuous supervision or a new daemon.

## Deliverable 1: architecture review

Provide a verdict with evidence: whether this design meets the simplification
objective, remaining implementation risks, and any new blocking issues.

Include:

- Current duplication and sample-pipeline coupling, with source references.
- Component boundaries, authored versus generated files, and a proposed source
  directory layout. Explain each new abstraction and why existing code cannot
  already serve that purpose.
- A precise proposed graph schema, type/template contract, normalized contract,
  and execution-target capability matrix. Define where defaults originate and
  how explicit graph settings are validated and resolved.
- Endpoint/listener assignment, connection cardinality, IPC locality, readiness,
  OVS attachment mapping, and resource naming rules.
- Platform behavior for graphs with native processes, raw protocols, external
  devices, no managed application containers, and multiple SDR/QEMU nodes.
- Build boundaries and credential provisioning references, including external
  device certificates versus generated simulator credentials.
- The execution boundary and failure handling that reuse current ownership
  checks without building a general-purpose orchestration system.

In `architecture.md`, include an artifact-to-owner table identifying each
artifact/action, its execution owner, invocation boundary, readiness input,
failure result, cleanup owner, and persisted state. Name the existing launcher
or infrastructure API that would consume each plan, or identify the smallest
narrowly scoped missing adapter. The compiler emits plans and never executes
them; adding an adapter must not introduce a general-purpose orchestrator or a
second lifecycle ledger.

Also include two traceability matrices:

1. I-01 through I-11 → proposed contract location, fixture paths, and planned
   verification.
2. Every scenario, feature variant, additional topology demonstration, and
   negative case → input, expected artifacts or diagnostic, execution target,
   and acceptance evidence with its evidence status.

These matrices index the concrete package; they do not replace it.

## Deliverable 2: concrete example review package

Use this required package root and top-level structure:

```text
design/graph-generation/
  README.md
  inventory.json
  architecture.md
  catalog/
  scenarios/
  variants/
  negative/
  verification.md
  implementation-plan.md
```

`inventory.json` indexes all scenarios, variants, additional topology
demonstrations, and negative cases by stable ID and relative path. Document its
field contract in `README.md`. Keep reusable type definitions in `catalog/`;
fixtures reference them without copying them. Put the architecture review and
traceability matrices in `architecture.md`, the phases in
`implementation-plan.md`, and check definitions/results in `verification.md`.

Do not place hand-authored fixtures under generated runtime/build directories.
Mark every file as design-only in its header or containing README, including
JSON and other formats that cannot carry comments. The root README must identify
what remains unimplemented and distinguish illustrative artifacts from checks
actually performed.

Each positive scenario, variant, and additional topology demonstration contains
`graphx.yml`, `README.md`, and `expected/inventory.json`. Each negative case
contains its input, README, and a machine-readable expected diagnostic. Per-case
inventories declare the target and placement, expected service/process counts
(including platform and auxiliary processes), expected file paths and formats,
and the completeness status of every artifact. If a case covers multiple targets,
index separate expected artifact sets for those targets.

Required expected YAML/JSON files must parse and must contain complete content,
not ellipses or omitted service sections. Native/OVS/QEMU actions may be complete
declarative command plans with their format defined in the package; they need
not be executable scripts. Supplemental pseudocode must be labeled and cannot
substitute for a required artifact. Document one placeholder syntax and an
explicit substitution table for intentionally unresolved values; use no secret
values. Define how typed placeholders are checked or substituted for static
validation. Parsing an artifact does not establish executable support.

Provide complete expected files for this review package. Repetition in expected
output is acceptable verification evidence; keep authored graph/type definitions
shared. Do not add a fixture generation framework merely to compress expected
boilerplate. Unsupported target/scenario combinations must identify a
machine-readable expected diagnostic instead of fabricated runtime artifacts.

For every scenario below, provide a complete proposed `graphx.yml`, its relevant
type definitions or references to shared definitions in the package, and expected
Compose and any required node/native/OVS/QEMU artifacts. Reuse shared definitions
across fixtures; do not hide topology-specific behavior in a generic template.
Do not substitute a coverage table alone for these concrete artifacts.

1. Sample pipeline.
2. Native shared memory, preserving queue capacity and message limits.
3. Native UDP unicast.
4. Native UDP multicast, preserving loopback and TTL semantics.
5. UDP broadcast on the declared isolated subnet.
6. Application capture.
7. MACVLAN semantic profile on OVS.
8. IPVLAN L2, including routing, policies, and mirrors.
9. IPVLAN L3, including shared MAC and forwarding semantics.
10. Mixed network profiles and routing.
11. Network observation with an external QEMU source that is not booted.
12. Static routes and policy, including the deliberately deferred route.
13. Simulated SDR with UDP samples, mTLS control, and TCP results.
14. External SDR, with the laboratory simulator represented separately.
15. QEMU TAP, including peer namespace, VLAN isolation, capture, and guest build
    and boot artifacts.

Also demonstrate:

- All six existing sample feature variants: history, observability stack,
  control, credential rotation, secure OTLP, and OTLP mTLS. History is already
  enabled by default; its variant demonstrates explicit settings or retention.
- Renamed sample nodes and edges, with a second independent source/processor
  pair, using unchanged shared templates.
- A multi-radio SDR topology and a mixed container/QEMU SDR topology. Use only
  supported fan-in, or show the exact validation rejection and a valid separate
  sink arrangement. Identify any guest application artifact that must be built;
  do not claim it already exists.
- Negative fixtures for ambiguous endpoints, invalid port/schema connections,
  unsupported fan-in or feedback, incompatible IPC placement, name/address/port
  conflicts, missing guest artifacts, and unsupported target capabilities.

For each scenario, give a capability matrix covering native Linux, native macOS,
OrbStack, and Lima. Each cell must be `supported placement`, `not applicable`, or
`unsupported`, with a reason and capability rule. For unsupported requests,
reference the expected diagnostic. For supported placements, distinguish
preserved current behavior from proposed support and give the generated
service/process count, preserved semantics, and planned acceptance checks.
Composite placement must identify both application and platform locations; a
native process with an OrbStack platform does not constitute wholly native
deployment. Native macOS does not provide OVS, and OrbStack evidence does not
establish privileged Linux/Lima acceptance. Current QEMU execution uses TCG;
KVM may be a proposed capability or planned test dimension, never an unsupported
claim of current behavior or evidence.

Use the evidence-status vocabulary defined in Deliverable 4 throughout the
package, including inventories, matrices, and per-scenario READMEs.

## Deliverable 3: implementation sequence

Provide small, dependency-ordered phases. Each phase must identify:

- Concrete outcome and files/components affected.
- The smallest reusable abstraction required.
- Obsolete files, duplicated configurations, and sample assumptions removed.
- Contract/schema/consumer changes that must land together.
- Focused tests, example acceptance, and completion criteria.

Cover the authoritative model/compiler, generic application bindings, reusable
images/templates, default platform, native and OVS execution, QEMU guest support,
all example conversions, and removal of superseded paths. Make the complete
example design review the gate before production implementation.

Avoid long-lived compatibility scaffolding, a new deployment daemon, a generic
plugin framework, parallel hand-maintained manifests, sample-specific compiler
branches, or per-topology image builds. Prefer fewer concepts and fewer authored
files, and explain any unavoidable additional mechanism.

## Deliverable 4: verification strategy

Label each check with one of these statuses, and record its environment and
result separately:

| Status | Meaning |
|---|---|
| `run-current` | An actually executed check against current implemented behavior, with command, environment, result, and evidence location |
| `static-design` | A static check of proposed fixtures, such as parsing, reference resolution, inventory completeness, or contract consistency; state whether it ran and its result |
| `planned-portable` | A future portable/native verification requirement, with target platforms specified |
| `planned-docker` | A future live container verification requirement, with engine specified |
| `planned-privileged` | A future authorized native Linux or Lima infrastructure check |
| `planned-guest-boot` | A future actual QEMU guest execution check, with architecture and accelerator specified |

Never label a proposed runtime feature `run-current` because its illustrative
configuration parsed. These labels do not authorize runtime or privileged
execution. Separate planned infrastructure and guest-boot evidence even when
one future acceptance scenario will exercise both.

Specify meaningful checks for deterministic generation, schema validation,
negative fixtures, node/edge renaming, multiple instances of one type, generated
Compose validity, default platform/history, credential isolation, and preservation
of each example's network and application behavior.

Follow `docs/test-procedure.md` and repository verification rules when those
checks become executable. Separate static fixture checks from live application
tests, privileged Linux/Lima networking, and actual QEMU guest boot. Do not claim
guest execution from TAP tests or privileged authorization from host OS alone.
Report checks actually run separately from planned verification.

For this design task, check YAML/JSON parseability, package inventory coverage,
relative links and references, placeholder declarations, expected diagnostic
coverage, and both traceability matrices using existing tools where available.
Record unavailable validators or missing proposed-schema checks explicitly;
do not implement a production compiler to validate the design. State exactly
which static checks ran, which remain pending, and their limitations.

## Review completion criteria

The review is complete when all 15 scenarios and six feature variants have
concrete consistent proposed artifacts, every accepted decision maps to a
contract and verification requirement, and the implementation phases are small
enough to assess independently. The renamed/multi-source example must require
no shared-template changes or sample-specific compiler logic. Any unsupported
configuration must have a precise rejection rather than a silent behavior change.

The required package inventory must account for every case and expected file;
artifacts must meet the completeness and parseability rules above. Evidence
statuses, platform capability cells, decision traceability, and execution
ownership must be explicit. Unperformed runtime tests remain planned and must
not be presented as a condition already verified by the design package.

Return a concise architectural verdict, links to the review package, the phased
implementation plan, and only genuinely new unresolved issues. Stop before
production implementation.
