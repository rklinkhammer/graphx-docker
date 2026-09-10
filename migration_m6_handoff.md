# Migration M6 implementation handoff

**Status:** Implemented; awaiting independent verification  
**Date:** 2026-09-10

## Delivered boundary

M6 extends version-2 `qemu_tap` attachments with required non-root `tap_uid` and
`tap_gid`. The GraphX ownership lifecycle now creates a persistent TAP, grants
that exact identity access, attaches it to system OVS, applies access/trunk VLAN
metadata, and records the TAP ifindex, alias, owner, OVS Port/Interface UUIDs,
graph/config markers, and owner token. Status, transactional rollback, explicit
recovery, and destroy validate the complete identity set and fail closed on
collision or replacement.

The new `examples/qemu-node/tap` profile runs the unchanged x86_64 Buildroot
guest as UID/GID 65532 on `gxqtap0`. It reuses QMP evidence, guest and peer
TCP/UDP services, the packet observer, telemetry-compatible records, and an OVS
SPAN capture. The launcher proves unicast, guest MAC learning, VLAN reachability
and isolation, broadcast/multicast forwarding, QMP pause/resume, a temporary
netem hook, and exact teardown. Runtime evidence stays in
`/var/lib/graphx/qemu/m6` inside Linux/Lima.

The existing external and container slirp profiles remain compatibility paths.
M6 does not create a Docker data-plane network and does not identify Apple
Silicon x86_64 TCG as KVM. Declarative capture/fault ownership remains M7.

## Implementation evidence

- The development build and focused QEMU/M6 portable tests pass.
- The privileged Lima lifecycle regression passes injected failure rollback,
  hard-crash recovery, replacement-Port preservation, and two clean cycles.
- The actual guest booted under x86_64 TCG in the ARM64 Lima VM. QMP reported
  KVM absent, TCP and UDP guest probes passed, the guest MAC appeared in OVS,
  VLAN isolation held, broadcast/multicast increased SPAN capture, QMP
  pause/resume was confirmed, netem was applied and cleared, and teardown left
  no managed networking or process residue.

## Independent verification focus

Rebuild from scratch and repeat the actual guest run. Broaden replacement tests
to the TAP itself and OVS Interface, inspect packet contents and observer history
rather than capture growth alone, and audit all retained M5/frozen-version-1
gates. Do not advance to M7 until that evidence is accepted.
