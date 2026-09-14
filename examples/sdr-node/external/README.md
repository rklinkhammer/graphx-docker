# External SDR and explicit laboratory selection

Complete the [CLI quick start](../../quick-start.md) to build `graphx` and prepare your environment.
Run from the repository root:

```sh
graphx example plan sdr-node/external
graphx example up sdr-node/external --allow-privileged --laboratory laboratory-radio
graphx example status sdr-node/external --allow-privileged
graphx example tokens sdr-node/external --allow-privileged
graphx example logs sdr-node/external --follow --allow-privileged
graphx example down sdr-node/external --allow-privileged
```

On macOS the CLI selects the existing identity-matched Lima VM; the default
console URL is http://127.0.0.1:18080. On native Linux it uses port 8080.

Physical-device startup remains gated; this command explicitly selects the isolated laboratory simulator and its test credentials.

The CLI prepares required verified artifacts and remembers compilation and state
paths. Use `--images DIR` or `--release DIR` to reuse existing verified artifacts;
Lima inputs refer to guest-local paths. Use `--restart` after changing source or
control selection. Cleanup removes only identity-owned resources and retains history.
