# Mixed semantic-network laboratory

This canonical M8 laboratory realizes MACVLAN and IPvlan-L2 semantics with two
system OVS bridges, container veth pairs, a Linux router namespace, nftables
policy, mirrors, declarative capture, and bounded netem faults. Docker Compose
provides management connectivity only; no Docker macvlan or ipvlan network is
created.

Run on native Linux or inside the GraphX Lima VM:

```sh
./build/dev/graphx validate examples/mixed-network/graphx.yaml
./build/dev/graphx infra create examples/mixed-network/graphx.yaml --dry-run
examples/mixed-network/scripts/up.sh
examples/mixed-network/scripts/status.sh
examples/mixed-network/scripts/down.sh
```

The common launcher starts `compose.yaml`, then applies the identity-recorded
OVS lifecycle. Startup rolls back resources created by a failed attempt;
teardown refuses identity drift. Active capture remains in bounded VM-local
storage according to the configuration retention policy.

The former native Docker-driver and Docker Desktop userspace-OVS simulations
were removed in M8. Their version-1 model remains only at
`examples/compatibility/v1/mixed-network.yaml` for deterministic migration.
