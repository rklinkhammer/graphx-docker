# IPvlan-L3 semantic-profile pipeline

This M8 laboratory represents three routed IPvlan-L3 semantic subnets with
system OVS, container veth pairs, and a Linux router namespace. Docker Compose
provides management connectivity only; GraphX does not create an ipvlan network.

```sh
./build/dev/graphx infra create examples/ipvlan-l3/graphx.yaml --dry-run
examples/ipvlan-l3/scripts/up.sh
examples/ipvlan-l3/scripts/status.sh
examples/ipvlan-l3/scripts/down.sh
```

Run on native Linux or in Lima. The version-1 Docker-driver input remains at
`examples/compatibility/v1/ipvlan-l3.yaml` solely for migration.
