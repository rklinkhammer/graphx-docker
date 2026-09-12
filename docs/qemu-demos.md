# QEMU TAP demonstration

The supported QEMU scenario is `examples/qemu-node/tap`. It runs an x86_64 guest
with a TAP device attached to a system Open vSwitch bridge. A Linux namespace peer,
access VLANs, OVS mirroring, packet observation, PCAPNG, SQLite packet history, and
QMP state evidence are part of the lab.

Build the guest image, then run the demo from the repository root on native Linux:

```sh
examples/qemu-node/scripts/build.sh
examples/qemu-node/scripts/demo.sh start
```

On macOS, build inside Lima with the [Lima QEMU procedure](../infrastructure/lima/README.md#run-the-qemu-tapovs-lab),
then invoke `demo.sh` at the macOS prompt for automatic guest dispatch.

Use `status`, `verify`, `pause`, `resume`, and `stop` with the same launcher. The
x86_64 guest uses TCG consistently on native Linux and in the ARM64 Lima VM. The
launcher never uses QEMU user networking. Runtime files remain under
`/var/lib/graphx/qemu` in the Linux runtime. GraphX owns one root-only `dumpcap`
ring beneath `/var/lib/graphx/captures`; the unprivileged observer receives bounded,
read-only PCAPNG snapshots and produces packet telemetry and SQLite history. It has
no access to the live capture directory.

See [`../examples/qemu-node/tap/README.md`](../examples/qemu-node/tap/README.md) for
requirements and evidence checks.
