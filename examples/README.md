# GraphX examples

Run commands from the repository root. Build local binaries with
`cmake --preset dev` and `cmake --build --preset dev`.

| Example | Linux | macOS | Run / verification |
|---|---|---|---|
| [Shared memory](shared-memory/README.md) | Local processes | Native processes | `examples/shared-memory/run.sh` |
| [UDP unicast](udp-unicast/README.md) | Local processes | Native processes | `examples/udp-unicast/run.sh` |
| [UDP multicast](udp-multicast/README.md) | Local processes | Native processes | `examples/udp-multicast/run.sh` |
| [Application capture](capture/README.md) | Local processes | Native processes | `examples/capture/run.sh` |
| [UDP broadcast](udp-broadcast/README.md) | Docker Compose; optional namespace acceptance | OrbStack Compose; namespace acceptance inside Lima | `examples/udp-broadcast/run.sh` |
| [MACVLAN](macvlan/README.md) | System OVS | Automatic Lima dispatch | `scripts/network-lab.sh macvlan up` |
| [IPVLAN L2](ipvlan-l2/README.md) | System OVS | Automatic Lima dispatch | `scripts/network-lab.sh ipvlan-l2 up` |
| [IPVLAN L3](ipvlan-l3/README.md) | System OVS | Automatic Lima dispatch | `scripts/network-lab.sh ipvlan-l3 up` |
| [Mixed network](mixed-network/README.md) | System OVS | Automatic Lima dispatch | `scripts/network-lab.sh mixed-network up` |
| [Network observation](network-observability/README.md) | Privileged CLI | Explicit Lima guest shell | Owned capture and timed fault lifecycle |
| [Static routes and policy](static-route-policy/README.md) | Privileged launcher | Explicit Lima guest shell | `examples/static-route-policy/scripts/demo.sh up` |
| [Simulated SDR](sdr-node/simulated/README.md) | Docker Compose | OrbStack Compose | `examples/sdr-node/simulated/scripts/demo.sh start` |
| [External SDR](sdr-node/external/README.md) | System OVS and Docker | Explicit Lima guest shell | `examples/sdr-node/external/scripts/demo.sh up` |
| [QEMU TAP](qemu-node/README.md) | System OVS, x86_64 guest with TCG | Build inside Lima; automatic runtime dispatch, TCG | `examples/qemu-node/scripts/demo.sh start` |

MACVLAN and IPVLAN are semantic profiles implemented by system Open vSwitch;
they are not Docker network drivers. Compose owns processes and management
connectivity. Privileged examples require root/sudo on Linux or inside the
[dedicated GraphX Lima VM](../infrastructure/lima/README.md).
Lima support requires Apple Silicon; no privileged data plane runs natively on
macOS, and OrbStack results do not establish OVS or native Linux acceptance.

For the four network profiles, use `plan`, `status`, and `down` with the same
lab name. For other examples, follow the linked README for verification and
cleanup. The network-observability configuration creates infrastructure only;
it does not boot the declared external QEMU packet source.

See [test procedure](../docs/test-procedure.md) for acceptance coverage and the
separate macOS, Lima, native Linux, TCG, and KVM evidence requirements.
