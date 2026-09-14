# Portable simulated SDR

Complete the [CLI quick start](../../quick-start.md) to build `graphx` and prepare your environment.
Run from the repository root:

```sh
graphx example plan sdr-node/simulated
graphx example up sdr-node/simulated
graphx example status sdr-node/simulated
graphx example tokens sdr-node/simulated
graphx example logs sdr-node/simulated --follow
graphx example down sdr-node/simulated
```

The CLI opens an authenticated console automatically. Use `graphx example open sdr-node/simulated` to reopen it without restarting.
Control is disabled unless explicitly granted or enabled in the authored graph.


The CLI prepares required verified artifacts and remembers compilation and state
paths. Use `--images DIR` or `--release DIR` to reuse existing verified artifacts;
Lima inputs refer to guest-local paths. Use `--restart` after changing source or
control selection. Cleanup removes only identity-owned resources and retains history.
