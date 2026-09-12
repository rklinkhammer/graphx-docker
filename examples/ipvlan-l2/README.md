# IPVLAN L2 semantic profile

This lab implements shared-parent MAC identity and L2 filtering with system OVS.
Compose provides management connectivity only. Use `scripts/up.sh`, `status.sh`, and
`down.sh` on Linux or in the GraphX Lima guest.

Run from the repository root on native Linux or Apple Silicon macOS:

```sh
scripts/network-lab.sh ipvlan-l2 plan
scripts/network-lab.sh ipvlan-l2 up
scripts/network-lab.sh ipvlan-l2 status
scripts/network-lab.sh ipvlan-l2 down
```

On macOS these commands dispatch into the identity-checked
[GraphX Lima guest](../../infrastructure/lima/README.md). On Linux they run
locally with sudo and system OVS. Compose supplies management connectivity;
GraphX attaches the data-plane veth pairs. These are OVS semantic profiles,
not Docker MACVLAN/IPVLAN drivers.
