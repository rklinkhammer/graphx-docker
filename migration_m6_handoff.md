# Migration M6 implementation handoff

**Status:** Remediated after independent verification; final independent acceptance required
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

## Independent-verification remediation

- Lima provisioning now installs and records Ubuntu's `docker-buildx` plugin;
  readiness, the authoritative Lima verifier, and portable static contracts all
  require it. The QEMU guest builder fails early with a clear prerequisite
  message if Buildx is unavailable.
- Provisioning adds the dedicated Lima login user to the rootful Docker group,
  proves access under that user identity, and documents the group's
  root-equivalent security boundary inside the dedicated VM.
- `start.sh` handles Lima's pre-provisioning SSH group snapshot: if ordinary-user
  Docker access is initially unavailable, it performs one bounded stop/start and
  then requires both Docker and Buildx to work without `sudo`.
- The stale VM was deliberately removed. A final clean ARM64/VZ instance was
  created from the remediated candidate, automatically refreshed its login
  session, and passed the authoritative Lima verifier.
- From that clean VM and ordinary login identity, the root project image and the
  QEMU Buildroot builder completed. Fresh x86_64 guest artifacts and legal-info
  output were produced.
- The M4 live container-veth regression passed with the normal
  `graphx-demo:latest` image. Two invocations of the M6 privileged lifecycle
  regression passed, followed by the actual QEMU TAP profile with TCG,
  TCP/UDP, VLAN/SPAN, QMP pause/resume, netem apply/remove, and exact teardown.

See `migration_m6_remediation.md` for the remediation evidence. This handoff
does not replace independent re-verification, and M7 remains blocked until M6
receives an independent passing verdict.
