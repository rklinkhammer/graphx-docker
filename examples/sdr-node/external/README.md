# External SDR profile

This profile uses a Linux namespace as the bounded external-device endpoint and
keeps the raw SDR edge outside the GraphX envelope transport factory. The demo
generates short-lived TLS material, realizes the OVS boundary, runs the services,
and removes only identity-owned resources during cleanup.

Platform: native Linux with sudo and system OVS, or the dedicated
[GraphX Lima guest](../../../infrastructure/lima/README.md) on Apple Silicon macOS.
This launcher does not dispatch from macOS automatically. After provisioning
and verifying Lima, enter the guest:

```sh
limactl shell --workdir /workspace/graphx-docker graphx
export GRAPHX_BIN=/var/lib/graphx/runtime/build/dev/graphx
```

Then run the following from the repository root inside that guest, or directly
on native Linux after building `build/dev/graphx`:

```sh
examples/sdr-node/external/scripts/demo.sh up
examples/sdr-node/external/scripts/demo.sh status
examples/sdr-node/external/scripts/demo.sh verify
examples/sdr-node/external/scripts/demo.sh down
```

The launcher starts only processor and sink containers plus the namespace SDR
simulator. It realizes the OVS mirror endpoint, but does not start a capture
observer, history database, telemetry service, or browser UI. Their configuration
metadata alone does not launch those services; use the simulated profile to
exercise that complete observation stack.
