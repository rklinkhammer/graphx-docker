# IPvlan-L2 semantic-profile pipeline

This M8 laboratory represents three IPvlan-L2 semantic domains with system OVS,
container veth pairs, and a GraphX-owned Linux router namespace. Docker Compose
supplies management connectivity only and creates no ipvlan data-plane network.

```sh
./build/dev/graphx infra create examples/ipvlan-l2/graphx.yaml --dry-run
examples/ipvlan-l2/scripts/up.sh
examples/ipvlan-l2/scripts/status.sh
examples/ipvlan-l2/scripts/down.sh
```

Run on native Linux or in Lima. The version-1 Docker-driver input remains at
`examples/compatibility/v1/ipvlan-l2.yaml` solely for migration.
