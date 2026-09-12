# QEMU TAP demonstration

The supported QEMU scenario is `examples/qemu-node/tap`. It runs an x86_64 guest
with a TAP device attached to a system Open vSwitch bridge. A Linux namespace peer,
access VLANs, OVS mirroring, packet observation, PCAPNG, SQLite packet history, and
QMP state evidence are part of the lab.

Build the guest image, then run the demo on native Linux or in the GraphX Lima VM:

```sh
examples/qemu-node/scripts/build.sh
examples/qemu-node/scripts/demo.sh start --accel auto
```

Use `status`, `logs`, and `stop` with the same launcher. `--accel auto` chooses KVM
when available and otherwise uses TCG. The launcher never falls back to QEMU user
networking. Runtime files remain under `/var/lib/graphx/qemu` in the Linux runtime.

See [`../examples/qemu-node/tap/README.md`](../examples/qemu-node/tap/README.md) for
requirements and evidence checks.
