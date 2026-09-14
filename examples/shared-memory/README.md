# Shared-memory process pipeline

Complete the [CLI quick start](../quick-start.md) to build `graphx` and prepare your environment.
Run from the repository root:

```sh
graphx example plan shared-memory
graphx example up shared-memory
graphx example status shared-memory
graphx example tokens shared-memory
graphx example logs shared-memory --follow
graphx example down shared-memory
```

Open the console URL printed by `up` and enter its observation token.
Control is disabled unless explicitly granted or enabled in the authored graph.


The CLI prepares required verified artifacts and remembers compilation and state
paths. Use `--images DIR` or `--release DIR` to reuse existing verified artifacts;
Lima inputs refer to guest-local paths. Use `--restart` after changing source or
control selection. Cleanup removes only identity-owned resources and retains history.
