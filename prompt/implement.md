# GraphX implementation work package

Implement **Migration M3: generic ownership and OVS lifecycle** in
`~/workspace/graphx-docker` after the independently verified M2 boundary.

## Objective

Introduce persistent, identity-safe version-2 run ownership and realize only
the OVS bridge layer. Keep normalized planning, OVS realization, endpoint
attachment, and state persistence separate. Do not implement container or
namespace veth attachment, QEMU TAP, profile flows, mirrors, routes, faults, or
capture realization; those remain M4 and later.

## Required implementation

- Store state under `/var/lib/graphx/runs` by default, with an explicit bounded
  override for tests.
- Use a mode-0700 non-symlink state root, mode-0600 non-symlink state files,
  atomic publication, and one non-blocking per-graph mutation lock.
- Record the literal configuration SHA-256, graph ID, cryptographically random
  owner token, lifecycle status, expected bridges, and stable OVS UUIDs.
- Create every bridge with `datapath_type=system` and owner token, configuration
  digest, and graph ID in OVS `external_ids` in one OVSDB transaction.
- Refuse existing state and every unowned name collision. Never adopt a bridge
  based on its name alone.
- Update state after each mutation. Recover a crash between the atomic OVS
  mutation and state update by matching intrinsic token/digest markers.
- Preflight the complete resource set before deletion and recheck UUID and
  markers atomically with each deletion. Preserve unrelated replacements.
- Provide create, status, destroy, recover, and non-mutating dry-run CLI paths.
- Keep version-1 plans and all five M0 fingerprints unchanged.

The persistent resource model must leave explicit extension points for later
ifindices, namespace inodes, container identities, TAP owners, routes, rules,
qdiscs, captures, and process identities. Do not fabricate identities for
resources M3 does not create.

## Verification

Test normal lifecycle, repeated create refusal, missing state, unowned
collisions, malformed/permissive/symlink state, concurrent operations,
configuration drift, missing owned resources, replacements with and without
copied markers, failure rollback, hard interruption after every bridge
mutation, recovery, repeated cleanup, and immediate retry. Prove no endpoint or
legacy Docker command is emitted. Run portable quick, quality, sanitizer,
fingerprint, projection, documentation, and real Lima system-OVS gates.

Write `migration_m3_handoff.md`. Do not commit, publish, or advance to M4.
