# MACVLAN semantic-profile pipeline

`macvlan` is a semantic L2 profile in M8. GraphX creates one system OVS bridge
and attaches each Compose-managed container with an owned veth pair; it does not
create a Docker macvlan network or touch a physical parent interface.

```sh
./build/dev/graphx infra create examples/macvlan/graphx.yaml --dry-run
examples/macvlan/scripts/up.sh
examples/macvlan/scripts/status.sh
examples/macvlan/scripts/down.sh
```

Run on native Linux or in the GraphX Lima VM. Compose supplies management
connectivity only. The version-1 input is retained at
`examples/compatibility/v1/macvlan.yaml` for migration, not execution.
