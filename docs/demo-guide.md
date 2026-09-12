# Demo guide

Complete the [`user guide`](user-guide.md) first for the shared build,
configuration, networking, capture, and telemetry model. This page only selects
the example that exercises a particular behavior.

The [complete example platform matrix](../examples/README.md) lists all transport,
capture, network, SDR, and QEMU examples with Linux and macOS execution paths.

Choose a current scenario by the behavior you want to observe:

| Scenario | What it demonstrates | Launcher |
|---|---|---|
| Portable pipeline | Graph transports, telemetry, history, and UI | `scripts/demo.sh` |
| OVS profiles | Ethernet, MACVLAN, and IPVLAN semantics | `scripts/network-lab.sh <lab> up` |
| Route and policy | Namespaces, routes, nftables, SPAN | `examples/static-route-policy/scripts/demo.sh` |
| SDR | Simulated or external-device processing | `examples/sdr-node/*/scripts/demo.sh` |
| QEMU | TAP/OVS guest networking and QMP evidence | `examples/qemu-node/scripts/demo.sh` |

Use `status` while a scenario is running and its matching `down` or `stop` action
for cleanup. OVS scenarios require native Linux or the GraphX Lima guest. The
portable pipeline runs with Docker Compose on Linux or OrbStack on macOS.

On Apple Silicon macOS, follow the
[`Docker and OVS with Lima`](../infrastructure/lima/README.md) guide. The network
and QEMU launchers run at the macOS prompt and dispatch privileged work to the
Lima VM automatically. External SDR and route-policy launchers require an
explicit guest shell; they do not dispatch automatically.

For browser-oriented checks see [`graphical-examples-guide.md`](graphical-examples-guide.md).
For exact prerequisites and non-interactive verification see
[`test-procedure.md`](test-procedure.md).
