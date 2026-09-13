# IPVLAN L2 semantic profile

This v3 graph supports validation, compilation and explicitly authorized OVS
execution on native Linux or the GraphX Lima guest. S07–S12 have
[Lima acceptance evidence](../../design/graph-generation/p7-verification.md).

```sh
build/dev/graphx config normalize examples/ipvlan-l2/graphx.yml --target lima
```

Compile with a verified shared-image release catalog, then set `GX_OUTPUT`,
`GX_STATE`, `GRAPHX_BIN` and `GRAPHX_IMAGE_RELEASE` to the compiled package,
state parent, Linux executable and image release. Runtime artifacts belong under
`/var/lib/graphx`. Set `GRAPHX_ALLOW_PRIVILEGED=1` only for an authorized run.

Use `scripts/network-lab.sh ipvlan-l2 up`, `status` or `down`; the example's
`scripts/up.sh`, `status.sh` and `down.sh` call the same compiled runner. On macOS,
all artifact paths refer to the existing Lima guest and `GRAPHX_LIMA_GRAPHX_BIN`
selects its executable. The dispatcher checks VM identity and never provisions it.

See [execution](../../docs/execution.md), [release packaging](../../docs/release-process.md)
and the [example matrix](../README.md) for prerequisites and supported targets.
