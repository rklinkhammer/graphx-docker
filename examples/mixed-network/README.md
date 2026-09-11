# Mixed semantic-network laboratory

This canonical M8 laboratory realizes MACVLAN and IPvlan-L2 semantics with two
system OVS bridges, container veth pairs, a Linux router namespace, nftables
policy, mirrors, declarative capture, and bounded netem faults. Docker Compose
provides management connectivity only; no Docker macvlan or ipvlan network is
created.

Run on native Linux or inside the GraphX Lima VM:

```sh
scripts/network-lab.sh mixed-network plan
scripts/network-lab.sh mixed-network up
scripts/network-lab.sh mixed-network status
scripts/network-lab.sh mixed-network down
```

The dispatcher runs locally on native Linux and through the identity-checked
GraphX Lima VM on Apple Silicon macOS. Run `infrastructure/lima/start.sh` and
`infrastructure/lima/verify.sh` once before the first macOS invocation. The
common launcher starts `compose.yaml`, then applies the identity-recorded
OVS lifecycle. Startup rolls back resources created by a failed attempt;
teardown refuses identity drift. Active capture remains in bounded VM-local
storage according to the configuration retention policy.

The former native Docker-driver and Docker Desktop userspace-OVS simulations
were removed in M8. Their version-1 model remains only at
`examples/compatibility/v1/mixed-network.yaml` for deterministic migration.
