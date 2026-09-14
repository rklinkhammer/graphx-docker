# Sample pipeline

Complete the [CLI quick start](../quick-start.md) to build `graphx` and prepare your environment.
Run from the repository root:

```sh
graphx example plan sample-pipeline
graphx example up sample-pipeline --control generator:pause,resume
graphx example status sample-pipeline
graphx example tokens sample-pipeline
graphx example logs sample-pipeline --follow
graphx example down sample-pipeline
```

Open the console URL printed by `up` and enter its observation token.
Control is disabled unless explicitly granted or enabled in the authored graph.

The pipeline emits every 500 ms and runs indefinitely until stopped.

The CLI prepares required verified artifacts and remembers compilation and state
paths. Use `--images DIR` or `--release DIR` to reuse existing verified artifacts;
Lima inputs refer to guest-local paths. Use `--restart` after changing source or
control selection. Cleanup removes only identity-owned resources and retains history.
