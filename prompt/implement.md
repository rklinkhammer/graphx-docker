# GraphX implementation work package

Implement **Migration M4: managed container veth attachment** in
`~/workspace/graphx-docker` after the independently verified M3 boundary.

## Objective

Keep Compose management connectivity separate from the GraphX data plane.
Resolve managed services by verified deployment identity, attach owned veth
pairs only through OVS, configure declared endpoint state, and fail closed on
container or namespace replacement.

## Required implementation

- Add an explicit bounded Compose project identity to version-2 deployment.
- Require explicit host peer, target interface, OVS switch, and reviewed static
  address for realization; support declared MAC, MTU, and endpoint routes.
- Resolve exactly one running container by Compose project and service labels.
  Verify its full ID, running state, PID, image, labels, and network-namespace
  inode before publishing ownership state or mutating the host.
- Preflight all host, OVS, and target-namespace names without adopting names.
- Create an aliased veth pair, record both ifindices, create intrinsically marked
  OVS Interface and Port rows, move the peer, and configure it in the recorded
  namespace.
- Persist OVS UUIDs, container ID, namespace inode, interface names, attachment
  identity, and declared endpoint intent in the M3 ownership ledger extension.
- Detect restart/replacement during status. Permit reattachment only through an
  explicit identity-safe destroy/create cycle.
- Roll endpoints back before bridges and recover an endpoint completed between
  mutation and ledger update from intrinsic markers.
- Preserve version-1 behavior and deterministic version-1 migration. Never
  create or join a Docker data-plane network.
- Keep namespace veth, QEMU TAP, profile flows, mirrors, general routing/policy,
  faults, and capture realization deferred.

## Verification

Run portable configuration, schema, migration, planning, ownership, full test,
quality, sanitizer, fingerprint, projection, and documentation gates. In Lima
or native Linux with rootful Docker and system OVS, prove two create/status/
destroy cycles, endpoint settings, intrinsic identities, restart detection,
replacement preservation, interruption/recovery, and exact cleanup.

Write `migration_m4_handoff.md`. Do not commit, publish, or advance to M5.
