# IPVLAN L2 semantic profile

This authored v3 example is supported for validation and normalization. Graph
execution is unavailable in P1; its launcher returns `E_PHASE_UNAVAILABLE` before
performing actions. The retained launch recipes require the later adapters.

Run from the repository root:

```sh
build/dev/graphx validate examples/ipvlan-l2/graphx.yml
build/dev/graphx config normalize examples/ipvlan-l2/graphx.yml > resolved.json
```

Select `--target native-linux`, `native-macos`, `orbstack`, or `lima` to check
placement capabilities. Managed OVS and QEMU require Linux or Lima; this command
does not run a laboratory. The graph declares its catalog, typed node instances,
connections, bounded platform policy and any explicit scenario actions.

See the [example matrix](../README.md) for all supported inputs and
[configuration contract](../../docs/configuration.md) for diagnostics and limits.
