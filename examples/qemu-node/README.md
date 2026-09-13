# QEMU node

The [TAP profile](tap/README.md) is an authored v3 graph with a declared x86_64
TCG guest, a namespace peer, OVS VLANs and bounded mirror capture. P1 validates and
normalizes this contract for `native-linux` and `lima` targets.

```sh
build/dev/graphx config normalize examples/qemu-node/tap/graphx.yml --target lima
```

Build and launch scripts return `E_PHASE_UNAVAILABLE`. The catalog guest recipe
and checksums are illustrative, unbuilt declarations. An actual guest boot,
configuration delivery, readiness and control are separate P8 acceptance gates.
The reusable guest source, packet observer, QMP and capture utilities retain their
unprivileged module checks. These checks do not establish guest execution.

Runtime state and high-I/O artifacts belong under `/var/lib/graphx` on Linux or in
the dedicated Lima guest. No privileged socket is forwarded to macOS. See the
[phase plan](../../design/graph-generation/implementation-plan.md).
