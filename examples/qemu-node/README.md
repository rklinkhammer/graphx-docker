# QEMU node

The supported QEMU example is the `tap` profile. It connects an x86_64 guest to a
system Open vSwitch bridge, captures mirrored Ethernet traffic, records packet
history, and collects QMP runtime evidence.

```sh
scripts/build.sh
scripts/demo.sh start --accel auto
scripts/demo.sh status
scripts/demo.sh stop
```

Run it on native Linux or through the dispatcher into the GraphX Lima guest. Runtime
artifacts stay under `/var/lib/graphx/qemu`.
