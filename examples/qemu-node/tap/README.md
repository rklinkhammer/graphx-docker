# QEMU TAP profile

Complete the [CLI quick start](../../quick-start.md) to build `graphx` and prepare your environment.
Run from the repository root:

```sh
graphx example plan qemu-node/tap
graphx example up qemu-node/tap --allow-privileged
graphx example status qemu-node/tap --allow-privileged
graphx example tokens qemu-node/tap --allow-privileged
graphx example logs qemu-node/tap --follow --allow-privileged
graphx example down qemu-node/tap --allow-privileged
```

On macOS the CLI selects the existing identity-matched Lima VM; the default
console URL is http://127.0.0.1:18080. On native Linux it uses port 8080.

This example boots the declared x86_64 TCG guest. Preparation includes verified native, platform and guest artifacts. Runtime state remains in Linux; TAP creation alone is not proof of guest boot.

The CLI prepares required verified artifacts and remembers compilation and state
paths. Use `--images DIR` or `--release DIR` to reuse existing verified artifacts;
Lima inputs refer to guest-local paths. Use `--restart` after changing source or
control selection. Cleanup removes only identity-owned resources and retains history.
