# Sample pipeline through Open vSwitch

Complete the [CLI quick start](../../quick-start.md) to build `graphx` and prepare your environment.
Run from the repository root:

```sh
graphx example plan sample-pipeline/ovs
graphx example up sample-pipeline/ovs --allow-privileged --control generator:pause,resume --control collector:reset
graphx example status sample-pipeline/ovs --allow-privileged
graphx example tokens sample-pipeline/ovs --allow-privileged
graphx example logs sample-pipeline/ovs --follow --allow-privileged
graphx example down sample-pipeline/ovs --allow-privileged
```

On macOS the CLI selects the existing identity-matched Lima VM; the default
console URL is http://127.0.0.1:18080. On native Linux it uses port 8080.

The pipeline emits every 500 ms and runs indefinitely until stopped.
Three identity-owned container veth pairs connect through `pipeline-switch`
on `10.73.0.0/24`. The Network path view includes `pipeline-data` and the OVS
switch on both connections. No physical NIC or external switch is attached.

The CLI prepares required verified artifacts and remembers compilation and state
paths. Use `--images DIR` or `--release DIR` to reuse existing verified artifacts;
Lima inputs refer to guest-local paths. Use `--restart` after changing source or
control selection. Cleanup removes only identity-owned resources and retains history.

Reset counters requires `--control collector:reset`. If this example is already
running with pause/resume only, repeat the `up` command above with `--restart`
to apply both grants and open a newly authenticated console.
Reset clears collected metrics; it does not restart the pipeline or erase history.
