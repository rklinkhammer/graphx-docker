# GraphX independent verification work package

Independently verify **Migration M4: managed container veth attachment** in
`~/workspace/graphx-docker`. Derive M4-001 through M4-010 from
`prompt/implement.md`, ADR 0017, and the M3 re-verification. Treat the handoff as
a claim and write `migration_m4_verification.md` without product fixes.

## Required checks

1. Build fresh and run portable, quality, sanitizer, fingerprint, projection,
   documentation, M2, and M3 regression gates.
2. Confirm dry-run declares Compose identity, veth, OVS port, address, and MTU,
   but no Docker network, MACVLAN/IPVLAN driver, namespace, TAP, mirror, fault,
   capture, or process mutation.
3. In Lima ARM64 Linux, verify exactly one service is selected by project and
   service labels and that image, full ID, PID, and namespace inode match.
4. Verify host/peer ifindices and aliases, OVS Port/Interface UUIDs and intrinsic
   markers, target MAC/address/MTU/routes, and an OVS-only data path.
5. Exercise two clean create/status/destroy cycles and prove management access
   remains separate from the GraphX data interface.
6. Restart and replace the service container. Status must fail closed and must
   not adopt the new namespace; explicit safe destroy/create must reattach it.
7. Exercise unowned host, target-interface, OVS Port, and OVS Interface
   collisions plus marker/UUID/ifindex replacement. Cleanup must preserve every
   unrelated replacement and preflight siblings before mutation.
8. Inject ordinary failure and hard interruption at each exposed endpoint stage;
   prove rollback/recovery and immediate retry with exact cleanup.
9. Confirm M2 migration stays deterministic and validates when legacy addresses
   are absent, while M4 realization refuses to invent an address.
10. Confirm documentation accurately defers namespace veth, TAP, profile flows,
    mirrors, general routing/policy, faults, and capture realization.

M4 passes only with real Docker namespace, Linux veth, and system-OVS evidence.
