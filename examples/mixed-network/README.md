# Mixed semantic network lab

This lab realizes MACVLAN and IPVLAN-L2 semantics on two system OVS bridges with a
namespace router, policy rules, mirrors, container veth attachments, and declared
edge paths. Use `scripts/up.sh`, `status.sh`, and `down.sh`.

Run from the repository root on native Linux or Apple Silicon macOS:

```sh
scripts/network-lab.sh mixed-network plan
scripts/network-lab.sh mixed-network up
scripts/network-lab.sh mixed-network status
scripts/network-lab.sh mixed-network down
```

On macOS these commands dispatch into the identity-checked
[GraphX Lima guest](../../infrastructure/lima/README.md). On Linux they run
locally with sudo and system OVS. Compose supplies management connectivity;
GraphX attaches the data-plane veth pairs. These are OVS semantic profiles,
not Docker MACVLAN/IPVLAN drivers.
