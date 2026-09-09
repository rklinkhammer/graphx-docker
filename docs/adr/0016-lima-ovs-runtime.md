# ADR 0016: Lima hosts the macOS GraphX network runtime

- Status: Accepted for the OVS migration
- Date: 2026-09-08

## Context

GraphX currently has two network execution models. Native Linux uses host OVS,
veth pairs, namespace routers, Docker macvlan/ipvlan networks, nftables, and
`tc`. macOS uses Docker Desktop bridge networks and a privileged containerized
OVS userspace datapath. The macOS profile is intentionally a simulation because
Docker Desktop does not expose a Linux host on which GraphX can manage the same
network namespaces, system OVS datapath, veth pairs, and TAP devices.

Maintaining separate realization paths makes platform results difficult to
compare and leaves no suitable macOS foundation for attaching both Docker
containers and QEMU guests to one OVS-controlled topology.

## Decision

Lima is the supported macOS execution boundary for privileged GraphX network
laboratories. A dedicated Linux VM runs:

- the GraphX CLI and infrastructure lifecycle;
- rootful Docker for managed application and service containers;
- Open vSwitch with the Linux system datapath;
- Linux namespaces, veth, TAP, routing, nftables, and `tc`;
- QEMU and packet-capture tools.

The macOS checkout is mounted into the VM as source. Docker data, OVS state,
QEMU disks, active captures, run ownership state, and other high-I/O runtime
data remain on the VM's native Linux filesystem. Only bounded operator-facing
artifacts are exported to the shared checkout.

Management services may be forwarded to macOS loopback. The Docker socket and
privileged network interfaces are not exposed more broadly than required by the
documented local workflow.

The existing Docker Desktop bridge/privileged-OVS profile remains a legacy
simulation during migration. It is not native-network acceptance evidence and
will not be the target for new GraphX network capabilities.

## Consequences

- macOS and native Linux use the same Linux network mechanisms and GraphX
  infrastructure implementation.
- GraphX network mutation occurs inside the Lima VM, not on the macOS host.
- Rootful Docker, OVS, TAP, and packet capture enlarge the VM trust boundary;
  provisioning and host exposure require explicit verification.
- Apple Silicon normally runs the current x86_64 QEMU guest through TCG inside
  the ARM64 VM. KVM results remain native-supported-Linux evidence only.
- The VM is disposable, but GraphX must still prove ownership, rollback, and
  exact cleanup of resources inside it.

## Alternatives considered

- Continue the Docker Desktop OVS container: rejected as the primary path
  because it preserves a second userspace realization and cannot provide the
  required host veth/TAP lifecycle.
- Run all GraphX infrastructure in one privileged container: rejected because
  it conflates process packaging with the Linux host network boundary and makes
  container and QEMU attachment less direct.
- Mutate macOS networking directly: rejected because OVS system datapaths,
  Linux namespaces, veth, nftables, and Linux TAP lifecycle are required.

