# GraphX implementation work package

Implement **Migration M6: QEMU TAP and OVS attachment** in
`~/workspace/graphx-docker` after the independently verified M5 boundary.

## Objective

Run the existing QEMU guest through a GraphX-owned TAP and system-OVS path on
native Linux and in Lima. QEMU must run without root and receive access to only
its declared TAP. Retain slirp as an explicit compatibility profile.

## Required implementation

- Extend strict version-2 QEMU TAP configuration with explicit non-root TAP UID
  and GID; preserve version-1 behavior and deterministic migration.
- Create, mark, record, inspect, recover, and exactly delete the TAP through the
  privileged ownership lifecycle. Record kernel ifindex/alias, TAP owner,
  OVS Port/Interface UUIDs, VLAN state, graph/config identity, and owner token.
- Refuse unowned name collisions and any replaced TAP, Port, Interface, bridge,
  or sibling before cleanup changes the managed set.
- Add a Linux/Lima QEMU profile that reuses the current Buildroot guest, QMP
  evidence, TCP/UDP services, packet observer, telemetry semantics, and SPAN.
- Prove guest MAC learning, TCP/UDP unicast, allowed broadcast/multicast,
  access-VLAN reachability, distinct-VLAN isolation, pause/resume, a bounded
  fault hook, and exact cleanup.
- Report Apple Silicon x86_64 emulation as TCG; never label it KVM.
- Keep capture and fault helpers explicit until M7 owns their declarative
  lifecycle. Do not add Docker data-plane networking to the M6 path.
- Add portable contract coverage and a privileged two-cycle Linux lifecycle
  regression with failure, hard-crash recovery, replacement refusal, and no
  residue.

## Verification

Run formatting, the full portable suite, quality/sanitizer, frozen fingerprint,
schema, migration, and documentation gates. In Lima or native Linux, run the
privileged lifecycle test and the actual QEMU TAP profile, including QMP
pause/resume and SPAN evidence.

Write `migration_m6_handoff.md`. Do not commit, publish, or advance to M7.
