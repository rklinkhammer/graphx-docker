# Declarative network observation and faults

Complete the [CLI quick start](../quick-start.md) to build `graphx` and prepare your environment.
Run from the repository root:

```sh
graphx example plan network-observability
graphx example up network-observability --allow-privileged
graphx example status network-observability --allow-privileged
graphx example tokens network-observability --allow-privileged
graphx example logs network-observability --follow --allow-privileged
graphx example down network-observability --allow-privileged
```

On macOS the CLI selects the existing identity-matched Lima VM; the default
console URL is http://127.0.0.1:18080. On native Linux it uses port 8080.

This graph owns a TAP and bounded capture; it does not boot a guest. Select the declared fault separately with `graphx example scenario network-observability --action source-delay --allow-privileged`.

The CLI prepares required verified artifacts and remembers compilation and state
paths. Use `--images DIR` or `--release DIR` to reuse existing verified artifacts;
Lima inputs refer to guest-local paths. Use `--restart` after changing source or
control selection. Cleanup removes only identity-owned resources and retains history.
