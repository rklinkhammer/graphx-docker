# MACVLAN semantic-profile pipeline

`macvlan` is a semantic L2 profile in M8. GraphX creates one system OVS bridge
and attaches each Compose-managed container with an owned veth pair; it does not
create a Docker macvlan network or touch a physical parent interface.

```sh
scripts/network-lab.sh macvlan plan
scripts/network-lab.sh macvlan up
scripts/network-lab.sh macvlan status
scripts/network-lab.sh macvlan down
```

The dispatcher runs locally on native Linux and through the identity-checked
GraphX Lima VM on Apple Silicon macOS. Run `infrastructure/lima/start.sh` and
`infrastructure/lima/verify.sh` once before the first macOS invocation. Compose
supplies management connectivity only. The version-1 input is retained at
`examples/compatibility/v1/macvlan.yaml` for migration, not execution.
