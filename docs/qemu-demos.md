# QEMU TAP demonstration

The supported QEMU scenario is `examples/qemu-node/tap`. It runs an x86_64 guest
with a TAP device attached to a system Open vSwitch bridge. A Linux namespace peer,
access VLANs, OVS mirroring, packet observation, PCAPNG, SQLite packet history, and
QMP state evidence are part of the lab.

Build the guest image, then run the demo on native Linux or in the GraphX Lima VM:

```sh
examples/qemu-node/scripts/build.sh
examples/qemu-node/scripts/demo.sh start
```

Use `status`, `verify`, `pause`, `resume`, and `stop` with the same launcher. The
x86_64 guest uses TCG consistently on native Linux and in the ARM64 Lima VM. The
launcher never uses QEMU user networking. Runtime files remain under
`/var/lib/graphx/qemu` in the Linux runtime.

See [`../examples/qemu-node/tap/README.md`](../examples/qemu-node/tap/README.md) for
requirements and evidence checks.
