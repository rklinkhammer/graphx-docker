# GraphX examples

| Example | Domains | Routing model | Platforms |
| --- | ---: | --- | --- |
| `capture` | local processes | TCP loopback with bounded PCAPNG capture | Linux and macOS |
| `shared-memory` | local IPC | bounded POSIX shared-memory rings | Linux and macOS |
| `udp-unicast` | one loopback domain | IPv4 UDP loopback | Linux and macOS |
| `udp-multicast` | one local multicast domain | administratively scoped IPv4 multicast | Linux and macOS |
| `udp-broadcast` | one isolated Docker domain | directed broadcast on a fixed internal subnet | Linux and macOS with Docker |
| `macvlan` | one semantic L2 domain | OVS + container veth | native Linux or Lima |
| `ipvlan-l2` | three, one per node | OVS + Linux router namespace | native Linux |
| `ipvlan-l3` | three semantic L3 subnets | OVS + Linux router namespace | native Linux or Lima |
| `mixed-network` | macvlan + IPvlan L2 semantics | OVS + Linux router namespace | native Linux or Lima |
| `qemu-node` | QEMU TAP Ethernet | OVS/TAP raw TCP/UDP node with capture and faults | native Linux or Lima; x86_64 guest |
| `sdr-node` | isolated bridge simulation or macvlan + OVS/SPAN | raw UDP IQ, mutual-TLS TCP control, raw TCP results | portable simulated profile; native-Linux external profile |
| `static-route-policy` | three OVS-backed L2 domains | namespace router, nftables deny policy, and explicit manual route | portable plan inspection; native-Linux runtime |
| `network-observability` | one OVS domain with mirror | declarative bounded PCAPNG capture and timed netem | portable plan inspection; native Linux and Lima runtime |

See the [`GraphX demo guide`](../docs/demo-guide.md) for each scenario's purpose,
prerequisites, commands, expected evidence, and cleanup procedure. Formal macOS
and Linux acceptance sequences are in the
[`manual test procedures`](../docs/manual-test-procedures.md).

In the privileged network labs, Compose supplies management connectivity only.
GraphX owns all OVS bridges, veth/TAP interfaces, namespaces, capture, and
fault state. Run the matching `scripts/down.sh` before switching labs.

The [`qemu-node`](qemu-node/README.md) example is deliberately different: it
models an application that does not use the GraphX wire protocol. TAP/OVS is
canonical; user-network external/container profiles are deprecated compatibility
demonstrations.

The [`sdr-node`](sdr-node/README.md) suite applies the same external raw-packet
pattern to one radio, processor, and sink. Its portable profile is a Docker
bridge simulation; its native-Linux profile is the OVS/SPAN acceptance path.

The [`static-route-policy`](static-route-policy/README.md) laboratory uses
receiver results, nftables counters, exact route state, mirrored capture, and
GUI diagnostics to distinguish allowed, denied, missing-route, and
route-applied outcomes.

The [`network-observability`](network-observability/README.md) laboratory owns
an OVS mirror capture and a self-expiring endpoint fault, then exports a
complete PCAPNG snapshot from VM-local storage.
