# Two-source SDR configuration

Platform: configuration validation and normalization on native Linux or macOS.
This example defines two independent source/controller pairs with distinct raw
sample/control edges, TLS file references, and source settings. It does not start
services, access hardware, or create Linux infrastructure.

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

This configuration has no launcher. Existing SDR processes have not adopted these
settings yet, and infrastructure ownership is not instance-scoped. It is not the
concurrent-instance runtime acceptance topology. See the
[configuration contract](../../../docs/configuration.md#instance-selection-and-sdr-source-settings).
