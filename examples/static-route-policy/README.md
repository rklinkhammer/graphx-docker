# Static route and policy lab

This v3 graph supports validation, compilation and explicitly authorized OVS
execution on native Linux or the GraphX Lima guest. S07–S12 have
[Lima acceptance evidence](../../design/graph-generation/p7-verification.md).

```sh
build/dev/graphx config normalize examples/static-route-policy/graphx.yml --target lima
```

Compile with a verified shared-image release catalog, then set `GX_OUTPUT`,
`GX_STATE`, `GRAPHX_BIN` and `GRAPHX_IMAGE_RELEASE` to the compiled package,
state parent, Linux executable and image release. Runtime artifacts belong under
`/var/lib/graphx`. Set `GRAPHX_ALLOW_PRIVILEGED=1` only for an authorized run.

Inside the authorized Linux environment, set `GX_RELEASE` to the verified native
installation containing `graphx-diagnostic`, then use `scripts/demo.sh up`, `status`
or `down`. `scripts/inspect.sh` prints the compiled plan. Deferred routes remain
absent at startup; `apply-route` and `clear-route` remain gated until P9.

See [execution](../../docs/execution.md), [release packaging](../../docs/release-process.md)
and the [example matrix](../README.md) for prerequisites and supported targets.
