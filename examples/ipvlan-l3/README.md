# IPVLAN L3 semantic profile

Complete the [CLI quick start](../quick-start.md) to build `graphx` and prepare your environment.
Run from the repository root:

```sh
graphx example plan ipvlan-l3
graphx example up ipvlan-l3 --allow-privileged
graphx example status ipvlan-l3 --allow-privileged
graphx example tokens ipvlan-l3 --allow-privileged
graphx example logs ipvlan-l3 --follow --allow-privileged
graphx example down ipvlan-l3 --allow-privileged
```

On macOS the CLI selects the existing identity-matched Lima VM; the default
console URL is http://127.0.0.1:18080. On native Linux it uses port 8080.


The CLI prepares required verified artifacts and remembers compilation and state
paths. Use `--images DIR` or `--release DIR` to reuse existing verified artifacts;
Lima inputs refer to guest-local paths. Use `--restart` after changing source or
control selection. Cleanup removes only identity-owned resources and retains history.
