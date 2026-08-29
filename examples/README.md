# GraphX network examples

| Example | Domains | Routing model | Platforms |
|---|---:|---|---|
| `macvlan` | one L2 domain | direct macvlan switching | native Linux |
| `ipvlan-l2` | three, one per node | OVS + Linux router namespace | native Linux |
| `ipvlan-l3` | three, one per node | shared IPvlan L3 parent | native Linux |
| `mixed-network` | macvlan + IPvlan L2 | OVS + Linux router namespace | native Linux; Docker Desktop simulation on macOS |
| `shared-memory` | local IPC | bounded POSIX shared-memory rings | Linux and macOS local processes |

Every processing service is deployed from a separate Compose project and joins
an externally created network. Run the `scripts/down.sh` or corresponding mixed
example teardown before switching examples because the native labs create host
interfaces, namespaces, and Docker networks.
