# Declarative network observation and faults

This v3 graph supports validation, compilation and explicitly authorized OVS
execution on native Linux or the GraphX Lima guest. S07–S12 have
[Lima acceptance evidence](../../design/graph-generation/p7-verification.md).

```sh
build/dev/graphx config normalize examples/network-observability/graphx.yml --target lima
```

Compile with a verified shared-image release catalog, then set `GX_OUTPUT`,
`GX_STATE`, `GRAPHX_BIN` and `GRAPHX_IMAGE_RELEASE` to the compiled package,
state parent, Linux executable and image release. Runtime artifacts belong under
`/var/lib/graphx`. Set `GRAPHX_ALLOW_PRIVILEGED=1` only for an authorized run.

Use `graphx run up|status|down --output COMPILED --state-root STATE --images IMAGES
--allow-privileged` in the authorized Linux environment. This graph owns a TAP and
bounded Ethernet capture. It does not boot a guest or apply its scenario fault at
startup. Select `graphx scenario run --action source-delay --output COMPILED
--state-root STATE --allow-privileged` to apply the declared timed fault.
`scenario clear` with the same action ID clears it early. The acceptance harness
injects bounded frames through the owned TAP. See [scenario execution](../../docs/scenarios.md).

See [execution](../../docs/execution.md), [release packaging](../../docs/release-process.md)
and the [example matrix](../README.md) for prerequisites and supported targets.
