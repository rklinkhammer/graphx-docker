# Standalone PCAPNG capture demo

Complete the [CLI quick start](../quick-start.md) to build `graphx` and prepare your environment.
Run from the repository root:

```sh
graphx example plan capture
graphx example up capture
graphx example status capture
graphx example tokens capture
graphx example logs capture --follow
graphx example down capture
```

Open the console URL printed by `up` and enter its observation token.
Control is disabled unless explicitly granted or enabled in the authored graph.


The CLI prepares required verified artifacts and remembers compilation and state
paths. Use `--images DIR` or `--release DIR` to reuse existing verified artifacts;
Lima inputs refer to guest-local paths. Use `--restart` after changing source or
control selection. Cleanup removes only identity-owned resources and retains history.
