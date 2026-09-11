# IPvlan-L3 semantic-profile pipeline

This M8 laboratory represents three routed IPvlan-L3 semantic subnets with
system OVS, container veth pairs, and a Linux router namespace. Docker Compose
provides management connectivity only; GraphX does not create an ipvlan network.

```sh
scripts/network-lab.sh ipvlan-l3 plan
scripts/network-lab.sh ipvlan-l3 up
scripts/network-lab.sh ipvlan-l3 status
scripts/network-lab.sh ipvlan-l3 down
```

The dispatcher runs locally on native Linux and through the identity-checked
GraphX Lima VM on Apple Silicon macOS. Run `infrastructure/lima/start.sh` and
`infrastructure/lima/verify.sh` once before the first macOS invocation. The
version-1 Docker-driver input remains at
`examples/compatibility/v1/ipvlan-l3.yaml` solely for migration.
