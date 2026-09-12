# IPVLAN L3 semantic profile

This lab implements routed IPVLAN semantics across three subnets using OVS flow
rules and Linux namespaces. Use `scripts/up.sh`, `status.sh`, and `down.sh` on Linux
or in the GraphX Lima guest.

Run from the repository root on native Linux or Apple Silicon macOS:

```sh
scripts/network-lab.sh ipvlan-l3 plan
scripts/network-lab.sh ipvlan-l3 up
scripts/network-lab.sh ipvlan-l3 status
scripts/network-lab.sh ipvlan-l3 down
```

On macOS these commands dispatch into the identity-checked
[GraphX Lima guest](../../infrastructure/lima/README.md). On Linux they run
locally with sudo and system OVS. Compose supplies management connectivity;
GraphX attaches the data-plane veth pairs. These are OVS semantic profiles,
not Docker MACVLAN/IPVLAN drivers.
