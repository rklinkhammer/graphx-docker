# GraphX network examples

| Example | Domains | Routing model | Platforms |
|---|---:|---|---|
| `macvlan` | one L2 domain | direct macvlan switching | native Linux |
| `ipvlan-l2` | three, one per node | OVS + Linux router namespace | native Linux |
| `ipvlan-l3` | three L3 subnets, one per node | one multi-subnet IPvlan network and parent | native Linux |
| `mixed-network` | macvlan + IPvlan L2 | OVS + Linux router namespace | native Linux; Docker Desktop simulation on macOS |
| `shared-memory` | local IPC | bounded POSIX shared-memory rings | Linux and macOS local processes |
| `qemu-node` | QEMU user-mode Ethernet | externally managed raw TCP/UDP node with PCAP and bounded SQLite packet history | Linux and macOS hosts; x86_64 guest |

Every processing service is deployed from a separate Compose project and joins
an externally created network. Run the `scripts/down.sh` or corresponding mixed
example teardown before switching examples because the native labs create host
interfaces, namespaces, and Docker networks.

The [`qemu-node`](qemu-node/README.md) example is deliberately different: it
models an application that does not use the GraphX wire protocol. QEMU owns the
guest lifecycle and packet capture while GraphX provides the topology reference.
