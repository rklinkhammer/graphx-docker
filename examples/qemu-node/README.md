# QEMU node

Use the [TAP example](tap/README.md) through the common CLI:

```sh
graphx example up qemu-node/tap --allow-privileged
graphx example status qemu-node/tap --allow-privileged
graphx example down qemu-node/tap --allow-privileged
```

Preparation builds verified native, platform and guest artifacts. To reuse a
combined native/guest installation, pass `--release DIR`, `--images DIR` and
`--catalog DIR` with its verified guest catalog. See the [quick start](../quick-start.md) and
[guest release guide](../../guests/README.md). The TAP example declares an x86_64
TCG guest; inspect QMP and guest telemetry for actual boot evidence.
