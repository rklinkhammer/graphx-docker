# Demo guide

Choose a current scenario by the behavior you want to observe:

| Scenario | What it demonstrates | Launcher |
|---|---|---|
| Portable pipeline | Graph transports, telemetry, history, and UI | `scripts/demo.sh` |
| OVS profiles | Ethernet, MACVLAN, and IPVLAN semantics | `examples/<lab>/scripts/up.sh` |
| Route and policy | Namespaces, routes, nftables, SPAN | `examples/static-route-policy/scripts/demo.sh` |
| SDR | Simulated or external-device processing | `examples/sdr-node/*/scripts/demo.sh` |
| QEMU | TAP/OVS guest networking and QMP evidence | `examples/qemu-node/scripts/demo.sh` |

Use `status` while a scenario is running and its matching `down` or `stop` action
for cleanup. OVS scenarios require native Linux or the GraphX Lima guest. The
portable pipeline runs with Docker Compose on Linux or OrbStack on macOS.

For browser-oriented checks see [`graphical-examples-guide.md`](graphical-examples-guide.md).
For exact prerequisites and non-interactive verification see
[`test-procedure.md`](test-procedure.md).
