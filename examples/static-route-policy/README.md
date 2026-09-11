# Static-route and policy laboratory

This laboratory uses system OVS domains, veth-attached containers, one Linux
router namespace, nftables policy, a mirror, and an explicitly manual route.
Compose provides management connectivity only.

```sh
examples/static-route-policy/scripts/demo.sh up
examples/static-route-policy/scripts/demo.sh status
examples/static-route-policy/scripts/demo.sh apply-route
examples/static-route-policy/scripts/demo.sh clear-route
examples/static-route-policy/scripts/demo.sh down
```

Run lifecycle commands on native Linux or in Lima. The launcher uses
`graphx.yaml`; the old version-1 input is retained under
`examples/compatibility/v1/static-route-policy.yaml` only for migration.
