# QEMU TAP + Open vSwitch profile (M6)

This Linux-only profile is the primary GraphX QEMU data-plane path. On macOS,
run it inside the `graphx` Lima guest: macOS is the control surface, and the
Lima VM—not a macOS container—owns the Linux kernel networking environment.

GraphX creates and exclusively tracks `br-qemu-tap`, the persistent `gxqtap0`
TAP, namespace veths, VLAN metadata, and the OVS SPAN mirror. QEMU runs as the
dedicated unprivileged `graphx-qemu` identity (UID/GID 65532) and receives only
the TAP it needs. The guest keeps its established address, MAC, TCP/UDP echo
service, QMP evidence, packet observer, and telemetry semantics.

The `external/` and `container/` user-mode networking profiles remain supported
compatibility paths. They are deliberately not used by this profile and do not
share its OVS data plane.

## Run inside Lima

Build GraphX and the guest image first, then:

```sh
./examples/qemu-node/tap/scripts/ovs-lab.sh up
./examples/qemu-node/tap/scripts/ovs-lab.sh status
./examples/qemu-node/tap/scripts/ovs-lab.sh pause
./examples/qemu-node/tap/scripts/ovs-lab.sh resume
./examples/qemu-node/tap/scripts/ovs-lab.sh fault-on
./examples/qemu-node/tap/scripts/ovs-lab.sh fault-off
./examples/qemu-node/tap/scripts/ovs-lab.sh verify
./examples/qemu-node/tap/scripts/ovs-lab.sh down
```

`up` proves guest TCP/UDP unicast, the configured guest MAC in the OVS forwarding
database, VLAN 42 reachability and VLAN 43 isolation, broadcast/multicast
forwarding into the switch, and SPAN capture growth. `pause` and `resume` use QMP
and require QEMU to confirm the resulting state. `fault-on` is an explicit M6
demonstration hook; declarative, ledger-owned fault policies remain M7 work.

Runtime evidence is kept on the Lima guest's native filesystem at
`/var/lib/graphx/qemu/m6`: QMP accelerator evidence, serial log, raw SPAN PCAP,
PCAPNG, and packet-history SQLite data. GraphX's infrastructure ledger remains
under `/var/lib/graphx/runs`. `down` stops only identity-checked processes before
asking GraphX to remove its owned TAP, ports, mirror, namespace, and bridge.

The profile selects x86_64 TCG because the checked-in Buildroot guest is x86_64.
It does not claim KVM acceleration on Apple Silicon; KVM is only meaningful when
the Lima guest and QEMU guest architecture can use it and QMP confirms it.
