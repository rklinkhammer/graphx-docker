# Demo guide

Use the [CLI quick start](../examples/quick-start.md) first. Every demo uses
`graphx example up NAME`, followed by `status`, `tokens`, `logs` or `down` with
the same name. Startup prints the console URL and observation/control credentials.

| Behavior | Startup command |
|---|---|
| Continuous portable pipeline | `graphx example up sample-pipeline --control generator:pause,resume` |
| Continuous OVS pipeline | `graphx example up sample-pipeline/ovs --allow-privileged --control generator:pause,resume` |
| IPVLAN routing | `graphx example up ipvlan-l3 --allow-privileged` |
| Route and policy diagnostics | `graphx example up static-route-policy --allow-privileged` |
| Simulated SDR | `graphx example up sdr-node/simulated` |
| Explicit laboratory SDR | `graphx example up sdr-node/external --allow-privileged --laboratory laboratory-radio` |
| QEMU guest | `graphx example up qemu-node/tap --allow-privileged` |

On macOS, the CLI selects OrbStack for portable containers and the identity-matched
GraphX Lima VM for privileged labs. Use `graphx env up` explicitly to start that VM.
Preparation, compilation and credential staging happen through the shared workflow;
physical-device startup remains gated. Follow the [complete example matrix](../examples/README.md)
for supported targets and [CLI reference](example-cli.md) for artifact overrides.

Stop a demo before starting another with the same console port. Select scenario
actions explicitly with `graphx example scenario NAME --action ID` and include
`--allow-privileged` for network labs. See [manual checks](manual-test-procedures.md)
and [test procedure](test-procedure.md) for verification requirements.
