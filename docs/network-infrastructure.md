# Network infrastructure layer

GraphX treats network infrastructure as a peer of logical topology, transport,
deployment, observability, and GUI/control. The versioned `network` section of
`graphx.yaml` owns these objects:

- `networks`: Docker bridge, macvlan, or ipvlan address domains;
- `interfaces`: node IP/MAC attachments, separate from GraphX ports;
- `switches`: Open vSwitch bridges, ports, VLAN access/trunk metadata, and mirrors;
- `routers`: Linux namespace or container router interfaces, routes, forwarding,
  and backend-neutral policies;
- `edge_paths`: ordered infrastructure hops for each logical GraphX edge.

The C++ loader validates references, IPv4 subnet membership, MAC syntax, VLAN
ranges, mirror output ports, router interfaces, and graph-edge path hops. The
`graphx inspect` command prints both the application and infrastructure models.

## Infrastructure lifecycle

On native Linux, `graphx infra create` creates veth pairs, OVS bridges and ports,
router namespaces, addresses, forwarding, nftables hooks, mirrors, and external
Docker networks. Container deployment remains a separate step. `destroy` reverses
that boundary and `status` shows OVS, namespace, qdisc, nftables, and Docker state.

Commands are executed without a shell. Review the exact plan on any platform:

```sh
./build/dev/graphx infra create examples/mixed-network/graphx.yaml --dry-run
```

The provisioner targets clean development labs. It does not yet persist state,
reconcile drift, or roll back a partially failed create operation.

## Faults and inspection

`graphx infra fault apply` places `tc netem` on a selected router interface:

```sh
sudo ./build/dev/graphx infra fault apply examples/mixed-network/graphx.yaml \
  --router domain-router --interface ipv --delay 20ms --jitter 3ms --loss 1%
sudo ./build/dev/graphx infra fault clear examples/mixed-network/graphx.yaml \
  --router domain-router --interface ipv
```

Each OVS bridge has a SPAN output. The native example exposes `cap-mac` and
`cap-ipv`; the macOS container exposes `mirror-mac` and `mirror-ipv` internally.
Use `tcpdump`, `dumpcap`, or Wireshark against those interfaces.

## macOS execution model

Docker's macvlan driver is Linux-only and explicitly unsupported by Docker
Desktop for macOS. Docker Desktop also keeps its Linux bridges inside its VM,
so a process on macOS cannot attach host veth devices directly. Containerizing
OVS does not remove those constraints.

The portable profile therefore uses two independently created Docker bridge
domains and a privileged OVS container. OVS runs with `datapath_type=netdev`
(userspace datapath), attaches one container interface to each OVS bridge, and
routes between bridge-local gateway interfaces. This provides real OVS bridge,
SPAN, routing, nftables, and netem exercises on Docker Desktop, but it is a
simulation of the native topology:

- it does not use Docker macvlan or ipvlan drivers;
- it does not demonstrate unique externally visible container MAC addresses;
- performance is lower than the native kernel datapath;
- `/dev/net/tun` and privileged containers must be allowed by Docker Desktop.

References: [Docker macvlan platform requirements](https://docs.docker.com/engine/network/drivers/macvlan/),
[Docker Desktop networking limitations](https://docs.docker.com/desktop/features/networking/networking-how-tos/),
and [Open vSwitch userspace datapath](https://docs.openvswitch.org/en/latest/intro/install/userspace/).
