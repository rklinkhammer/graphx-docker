# GraphX examples

**Start here: [Run an example](quick-start.md).** Complete commands for sample-pipeline
and simulated SDR, including image preparation, console access and cleanup.

All 26 authored v3 examples support validation, normalization and deterministic
compilation on their listed targets. Native and portable container graphs support [owned execution](../docs/user-guide.md#execution-administration).
OVS and namespace graphs have a compiled runner behind explicit Linux privileged
opt-in, with [P7 Lima acceptance evidence](../design/graph-generation/p7-verification.md).
Container network and static-route startup wrappers use the compiled runner. Verified QEMU guest startup is supported; [P8](../design/graph-generation/p8-verification.md) records actual Lima TCG boot. [Scenario actions](../docs/user-guide.md#scenario-actions) are selected explicitly after baseline startup; the laboratory simulator is an explicit pre-start compilation selection. See [compilation instructions](../docs/user-guide.md#compile-inspectable-artifacts). Images use the [shared release recipes](../docs/user-guide.md#release-administration-shared-images-and-catalog-pins);
Compose is generated only in compiler output; examples contain no source Compose files. Run from the repository root:

```sh
build/dev/graphx config normalize examples/sample-pipeline/graphx.yml --target orbstack
```

| Input | Supported validation targets |
|---|---|
| [four-radio-vita/graphx.yml](four-radio-vita/graphx.yml) | `native-linux`, `lima` |
| [capture/graphx.yml](capture/graphx.yml) | `native-linux`, `native-macos`, `lima` |
| [ipvlan-l2/graphx.yml](ipvlan-l2/graphx.yml) | `native-linux`, `lima` |
| [ipvlan-l3/graphx.yml](ipvlan-l3/graphx.yml) | `native-linux`, `lima` |
| [macvlan/graphx.yml](macvlan/graphx.yml) | `native-linux`, `lima` |
| [mixed-network/graphx.yml](mixed-network/graphx.yml) | `native-linux`, `lima` |
| [network-observability/graphx.yml](network-observability/graphx.yml) | `native-linux`, `lima` |
| [qemu-node/tap/graphx.yml](qemu-node/tap/graphx.yml) | `native-linux`, `lima` |
| [sample-pipeline/graphx.yml](sample-pipeline/graphx.yml) | `native-linux`, `orbstack`, `lima` |
| [sample-pipeline/ovs/graphx.yml](sample-pipeline/ovs/graphx.yml) | `native-linux`, `lima` |
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
Use `graphx example up|open|status|tokens|logs|down NAME` for every demo.
The [CLI reference](../docs/user-guide.md#cli-reference) describes preparation and artifact overrides.
See [configuration](../docs/user-guide.md#configuration-reference), [documentation index](../docs/README.md),
and [test procedure](../docs/test-procedure.md).

The prior [acceptance record](../design/graph-generation/p10-verification.md) maps its 24-input inventory
to actual macOS, OrbStack, Linux ARM64/Lima and x86_64 TCG results. It also records
the native Linux x86_64 and clean-release gates that remain open.

The additional OVS sample and current browser-session workflow have separately
recorded evidence; see [verification scope](../docs/documentation-verification.md).
The matrix above is the current validation inventory, not a fresh release qualification.

The [four-radio VITA example](four-radio-vita/README.md) includes the explicitly
selected `iq-loss-jitter` scenario, normal image-role selection, supported FFT
settings and whole-graph recovery. Its [requirement matrix](../design/four-radio-vita/verification.md)
separates functional, sustained and browser evidence. No fault runs at startup.
