# GraphX implementation work package

Implement **Migration M2: configuration version 2 and semantic network
profiles** in `~/workspace/graphx-docker`. M2 follows ADR 0017, retains the M0
version-1 baseline, and uses the Lima environment delivered by M1 for the Linux
regression gate. The sequence is recorded in
`prompt/ovs_migration_implementation_plan.md`.

## Objective

Introduce an explicit configuration-version boundary for the future OVS-only
data plane. Version 1 must keep its Docker `bridge`/`macvlan`/`ipvlan` meaning.
Version 2 must express fixed semantic profiles and typed endpoint attachments
without realizing them through the legacy Docker planner.

M2 models and validates intent only. Do not implement persistent OVS ownership,
container veth movement, QEMU TAP realization, or packet-level profile behavior;
those belong to M3–M6.

## Required implementation

- Accept only integer configuration versions 1 and 2 in both the authoritative
  loader and JSON Schema.
- Retain version-1 parsing and realization without changing its generated
  command fingerprints.
- Add profiles `ethernet`, `macvlan`, `ipvlan-l2`, `ipvlan-l3`, and
  `ipvlan-l3s`.
- Define exact MAC identity, learning, filtering, ARP, broadcast, multicast,
  routing, isolation, and management behavior for every profile.
- Add attachments `container_veth`, `namespace_veth`, `qemu_tap`, `external`,
  and `mirror` with strict kind-specific fields and ownership rules.
- Validate network, node, router, switch, router-interface, mirror ID, mirror
  output-port, address/subnet, and runtime references. Never accept two
  contradictory descriptions of the same attachment.
- Require version-2 OVS switches to use the Linux system datapath.
- Forbid version-1 driver fields, `network.interfaces`, and
  `deployment.network` in version 2.
- Refuse all version-2 infrastructure, route, and fault realization with an M3
  diagnostic before any legacy Docker command is generated.

## Migration command

Provide `graphx config migrate [SOURCE] [--output FILE]`.

- Parse and validate the literal source document as version 1. Do not consume
  `GRAPHX_OVERRIDES` or other runtime overrides.
- Map every supported v1 driver/mode to exactly one semantic profile.
- Map node, router, QEMU, and mirror intent to typed attachments.
- Remove Compose data-plane membership.
- Emit byte-identical output for byte-identical input and build.
- Refuse ambiguous legacy meaning, non-system OVS switches, generated-ID
  collisions, oversized input, the source path, existing output, and symlinks.
- Use exclusive owner-only file creation and remove partial output after an
  error. Never modify the source.

## Tests and evidence

- Test every exact field of all five profile contracts.
- Test all attachment kinds and every kind-specific required/forbidden field.
- Adversarially test unknown and mismatched networks, owners, routers, switches,
  router interfaces, mirror IDs, and mirror output interfaces.
- Test quoted/non-integer versions against both the C++ loader and JSON Schema.
- Run migration twice for the root, macvlan, ipvlan-l2, ipvlan-l3,
  mixed-network, static-route-policy, and qemu-node configurations. Add an
  explicit ipvlan-l3s case.
- Run migration with hostile `GRAPHX_OVERRIDES` and prove identical output.
- Test safe output refusal and all v2 fail-closed realization entry points.
- Run the portable quick, quality, and sanitizer profiles; projection checks;
  all five M0 fingerprints; and JSON Schema validation in Lima.
- Run the retained Lima M1 verification once as a regression gate. Do not call
  its primitive packet exchange evidence for M2 profile semantics.

## Acceptance identifiers

- **M2-001 Boundary:** integer versions and version-aware keys preserve the
  explicit v1/v2 compatibility event.
- **M2-002 Profiles:** all five profiles expose exact, reviewable behavior.
- **M2-003 Attachments:** all five attachment kinds are strict and internally
  consistent with referenced topology objects.
- **M2-004 Migration:** literal v1 input migrates deterministically and
  reviewably without ambient override influence.
- **M2-005 Output safety:** migration is non-destructive and refuses unsafe
  destinations.
- **M2-006 Compatibility:** v1 tests, projections, and M0 fingerprints remain
  unchanged.
- **M2-007 Fail closed:** no v2 command reaches legacy Docker realization.
- **M2-008 Surfaces:** schema, inspect, and projections agree with the typed
  model.
- **M2-009 Documentation:** configuration, migration, upgrade/rollback,
  security, support, and architecture boundaries are explicit.
- **M2-010 Gates:** portable, quality, sanitizer, schema, compatibility, and
  Lima regression evidence passes.

## Required handoff

Write `migration_m2_handoff.md` with the acceptance matrix, changed paths,
exact commands and results, fingerprints, Lima evidence location, limitations,
and independent-verifier instructions. Do not commit, push, publish, or advance
to M3. M2 is implementation-complete only when every M2 gate passes and the
worktree is ready for an independent verification pass.
