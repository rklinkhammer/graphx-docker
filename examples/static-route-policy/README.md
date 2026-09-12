# Static route and policy lab

This scenario combines three OVS domains, a forwarding namespace, nftables policy,
mirrors, and a route that is activated explicitly through `graphx infra route`.
Use `scripts/demo.sh up`, `status`, `apply-route`, `clear-route`, and `down`.

Platform: native Linux with sudo and system OVS, or the dedicated
[GraphX Lima guest](../../infrastructure/lima/README.md) on Apple Silicon macOS.
This launcher does not dispatch from macOS automatically. After provisioning
and verifying Lima, enter the guest:

```sh
limactl shell --workdir /workspace/graphx-docker graphx
export GRAPHX_BIN=/var/lib/graphx/runtime/build/dev/graphx
```

Then run the following from the repository root inside that guest, or directly
on native Linux after building `build/dev/graphx`:

```sh
examples/static-route-policy/scripts/demo.sh up
examples/static-route-policy/scripts/demo.sh status
examples/static-route-policy/scripts/demo.sh apply-route
examples/static-route-policy/scripts/demo.sh clear-route
examples/static-route-policy/scripts/demo.sh down
```
