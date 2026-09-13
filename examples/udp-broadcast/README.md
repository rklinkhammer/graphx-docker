# UDP broadcast example

This authored v3 graph supports compilation and owned portable execution.
Compile it with a verified release, then use `graphx run up|status|down` as
described in the repository's `docs/execution.md`. The local launcher delegates
to that adapter and requires explicit compiled output and state roots.

Run from the repository root:

```sh
build/dev/graphx validate examples/udp-broadcast/graphx.yml
build/dev/graphx config normalize examples/udp-broadcast/graphx.yml > resolved.json
```

Select `--target native-linux`, `native-macos`, `orbstack`, or `lima` to check
placement capabilities. Managed OVS and QEMU require Linux or Lima; this command
does not run a laboratory. The graph declares its catalog, typed node instances,
connections, bounded platform policy and any explicit scenario actions.

See the [example matrix](../README.md) for all supported inputs and
[configuration contract](../../docs/configuration.md) for diagnostics and limits.
