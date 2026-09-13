# GraphX examples

All 24 authored v3 examples support validation and normalization. Execution
launchers return `E_PHASE_UNAVAILABLE` in P1. Run from the repository root:

```sh
build/dev/graphx config normalize examples/sample-pipeline/graphx.yml --target orbstack
```

| Input | Supported validation targets |
|---|---|
| [capture/graphx.yml](capture/graphx.yml) | `native-linux`, `native-macos`, `lima` |
| [ipvlan-l2/graphx.yml](ipvlan-l2/graphx.yml) | `native-linux`, `lima` |
| [ipvlan-l3/graphx.yml](ipvlan-l3/graphx.yml) | `native-linux`, `lima` |
| [macvlan/graphx.yml](macvlan/graphx.yml) | `native-linux`, `lima` |
| [mixed-network/graphx.yml](mixed-network/graphx.yml) | `native-linux`, `lima` |
| [network-observability/graphx.yml](network-observability/graphx.yml) | `native-linux`, `lima` |
| [qemu-node/tap/graphx.yml](qemu-node/tap/graphx.yml) | `native-linux`, `lima` |
| [sample-pipeline/graphx.yml](sample-pipeline/graphx.yml) | `native-linux`, `orbstack`, `lima` |
| [sdr-node/external/graphx.yml](sdr-node/external/graphx.yml) | `native-linux`, `lima` |
| [sdr-node/simulated/graphx.yml](sdr-node/simulated/graphx.yml) | `native-linux`, `orbstack`, `lima` |
| [shared-memory/graphx.yml](shared-memory/graphx.yml) | `native-linux`, `native-macos`, `lima` |
| [static-route-policy/graphx.yml](static-route-policy/graphx.yml) | `native-linux`, `lima` |
| [udp-broadcast/graphx.yml](udp-broadcast/graphx.yml) | `native-linux`, `orbstack`, `lima` |
| [udp-multicast/graphx.yml](udp-multicast/graphx.yml) | `native-linux`, `native-macos`, `lima` |
| [udp-unicast/graphx.yml](udp-unicast/graphx.yml) | `native-linux`, `native-macos`, `lima` |
| [variants/control/graphx.yml](variants/control/graphx.yml) | `native-linux`, `orbstack`, `lima` |
| [variants/credential-rotation/graphx.yml](variants/credential-rotation/graphx.yml) | `native-linux`, `orbstack`, `lima` |
| [variants/history/graphx.yml](variants/history/graphx.yml) | `native-linux`, `orbstack`, `lima` |
| [variants/mixed-container-qemu-sdr/graphx.yml](variants/mixed-container-qemu-sdr/graphx.yml) | `native-linux`, `lima` |
| [variants/multi-radio/graphx.yml](variants/multi-radio/graphx.yml) | `native-linux`, `orbstack`, `lima` |
| [variants/observability/graphx.yml](variants/observability/graphx.yml) | `native-linux`, `orbstack`, `lima` |
| [variants/otlp-mtls/graphx.yml](variants/otlp-mtls/graphx.yml) | `native-linux`, `orbstack`, `lima` |
| [variants/renamed-multi-source/graphx.yml](variants/renamed-multi-source/graphx.yml) | `native-linux`, `orbstack`, `lima` |
| [variants/secure-otlp/graphx.yml](variants/secure-otlp/graphx.yml) | `native-linux`, `orbstack`, `lima` |

Linux and Lima are validation target names, not execution claims. OVS uses owned
veth/TAP resources; MACVLAN/IPVLAN are semantic profiles. Compose owns processes
and management connectivity. No Docker engine or VM is needed for normalization.

Catalog images and guest artifacts remain illustrative until release packaging.
Retained Compose recipes and launcher bodies are not a supported v3 launch path.
See [configuration](../docs/configuration.md), [phase status](../design/graph-generation/p1-verification.md),
and [test procedure](../docs/test-procedure.md).
