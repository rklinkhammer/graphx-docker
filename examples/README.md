# GraphX examples

| Example | Domains | Routing model | Platforms |
| --- | ---: | --- | --- |
| `capture` | local processes | TCP loopback with bounded PCAPNG capture | Linux and macOS |
| `shared-memory` | local IPC | bounded POSIX shared-memory rings | Linux and macOS |
| `udp-unicast` | one loopback domain | IPv4 UDP loopback | Linux and macOS |
| `udp-multicast` | one local multicast domain | administratively scoped IPv4 multicast | Linux and macOS |
| `udp-broadcast` | one isolated Docker domain | directed broadcast on a fixed internal subnet | Linux and macOS with Docker |
| `macvlan` | one L2 domain | direct macvlan switching | native Linux |
| `ipvlan-l2` | three, one per node | OVS + Linux router namespace | native Linux |
| `ipvlan-l3` | three L3 subnets, one per node | one multi-subnet IPvlan network and parent | native Linux |
| `mixed-network` | macvlan + IPvlan L2 | OVS + Linux router namespace | native Linux; Docker Desktop simulation on macOS |
| `qemu-node` | QEMU user-mode Ethernet | external or Linux-containerized raw TCP/UDP node with PCAP and bounded SQLite packet history | Linux and macOS hosts; x86_64 guest |
| `sdr-node` | isolated bridge simulation or macvlan + OVS/SPAN | raw UDP IQ, mutual-TLS TCP control, raw TCP results | portable simulated profile; native-Linux external profile |

In the native network labs, every processing service is deployed from a separate
Compose project and joins an externally created network. Run the matching
`scripts/down.sh` or mixed-network teardown before switching labs because they
create host interfaces, namespaces, and Docker networks.

The [`qemu-node`](qemu-node/README.md) example is deliberately different: it
models an application that does not use the GraphX wire protocol. Its external
and Linux-container profiles share one guest and packet observer while GraphX
provides topology, telemetry, capture/history access, and scoped origin control.

The [`sdr-node`](sdr-node/README.md) suite applies the same external raw-packet
pattern to one radio, processor, and sink. Its portable profile is a Docker
bridge simulation; its native-Linux profile is the OVS/SPAN acceptance path.
