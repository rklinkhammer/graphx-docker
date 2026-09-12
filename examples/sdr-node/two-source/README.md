# Two-source SDR configuration

Platform: native validation/normalization on Linux or macOS; deployment with
Docker Compose on Linux or OrbStack on macOS.
This example defines two independent source/controller pairs with distinct raw
sample/control edges, TLS file references, and source settings. The shared instance launcher runs all four processes with generated mutual-TLS
credentials. It does not access physical hardware or create OVS infrastructure.

From the repository root after building GraphX:

```sh
build/dev/graphx validate examples/sdr-node/two-source/graphx.yaml
build/dev/graphx config normalize examples/sdr-node/two-source/graphx.yaml
build/dev/graphx config normalize examples/sdr-node/two-source/graphx.yaml \
  --set deployment.instance_id=lab-b
```

The first instance is `lab-a`; the override resolves the same graph as `lab-b`.
Consumers select `sdr-east`, `processor-east`, `sdr-west`, or `processor-west` by
node ID and the expected instance. No credentials need to exist for normalization;
the `/run` paths represent runtime-mounted files and contain no secret values.

The shared SDR source and controller processes consume these typed settings when
started with `GRAPHX_NORMALIZED_CONFIG`, `GRAPHX_NODE_ID`, a registered
`GRAPHX_EXECUTION_ID`, and the selected node's telemetry credential. Sources select
their own `sdr` block; controllers select the source connected through their control
edge. This configuration has no result sink, so controllers print processed
results locally. Its controllers declare `control: origin` and expose authenticated GUI
pause/resume for their respective sources. The source nodes remain observation-only.

The portable `graphx-sdr-instance-runtime` test provisions temporary TLS files and
runs both sources concurrently, checking their distinct frequencies, UDP ports,
and signed execution identities. It runs on native Linux or macOS without OVS.

Run the four graph processes, collector, and private namespace holder with:

```sh
python3 scripts/instance.py up examples/sdr-node/two-source/graphx.yaml --build --port 28081
python3 scripts/instance.py status examples/sdr-node/two-source/graphx.yaml
python3 scripts/instance.py restart examples/sdr-node/two-source/graphx.yaml --node processor-east
python3 scripts/instance.py down examples/sdr-node/two-source/graphx.yaml
```

Use `--set deployment.instance_id=lab-b` and another published console port for a
second deployment. Repeat the instance selection on every command. Each instance
has a private shared network namespace, so its loopback sample/control ports can
remain unchanged. Startup generates separate TLS credentials for each source pair;
controllers print processed samples at 100 MHz and 200 MHz respectively.
See [shared Compose runtime](../../../docs/compose-runtime.md) for credential,
storage, interruption, restart, and platform boundaries. Run `scripts/verify.sh instances` for the concurrent two-instance resilience proof.
See [instance acceptance](../../../docs/instance-acceptance.md) for its assertions,
Linux/macOS commands, and evidence boundaries.
