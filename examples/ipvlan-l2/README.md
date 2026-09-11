# IPvlan-L2 semantic-profile pipeline

This M8 laboratory represents three IPvlan-L2 semantic domains with system OVS,
container veth pairs, and a GraphX-owned Linux router namespace. Docker Compose
supplies management connectivity only and creates no ipvlan data-plane network.

```sh
scripts/network-lab.sh ipvlan-l2 plan
scripts/network-lab.sh ipvlan-l2 up
scripts/network-lab.sh ipvlan-l2 status
scripts/network-lab.sh ipvlan-l2 down
```

The dispatcher runs locally on native Linux and through the identity-checked
GraphX Lima VM on Apple Silicon macOS. Run `infrastructure/lima/start.sh` and
`infrastructure/lima/verify.sh` once before the first macOS invocation. The
version-1 Docker-driver input remains at
`examples/compatibility/v1/ipvlan-l2.yaml` solely for migration.
