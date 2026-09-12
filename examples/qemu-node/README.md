# QEMU node

The supported QEMU example is the `tap` profile. It connects an x86_64 guest to a
system Open vSwitch bridge, captures mirrored Ethernet traffic, records packet
history, and collects QMP runtime evidence.

```sh
examples/qemu-node/scripts/build.sh
examples/qemu-node/scripts/demo.sh start
examples/qemu-node/scripts/demo.sh status
examples/qemu-node/scripts/demo.sh stop
```

Run it on native Linux or through the dispatcher into the GraphX Lima guest. Runtime
artifacts stay under `/var/lib/graphx/qemu`. On macOS, build and run it using the
[`Docker and OVS with Lima`](../../infrastructure/lima/README.md) procedure.
