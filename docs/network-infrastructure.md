# Network infrastructure layer

Use the [`complete user guide`](user-guide.md#6-configure-networking) for the
supported operational workflow. This page is the detailed ownership and lifecycle
reference.

GraphX treats network infrastructure as a peer of logical topology, transport,
platform policy and GUI/control. The versioned `network` section of
`graphx.yml` owns these objects:

- `networks`: semantic Ethernet, macvlan, or ipvlan address domains realized by OVS;
- `switches`: Open vSwitch bridges, ports, VLAN access/trunk metadata, and mirrors;
- `routers`: Linux namespace router interfaces, routes, forwarding,
  and backend-neutral policies;
- `attachments`: container veth, namespace veth, QEMU TAP, and mirror endpoints;
- `edge_paths`: ordered infrastructure hops for each logical GraphX edge.
- `captures`: bounded Ethernet PCAPNG observers attached to mirror endpoints;
- `scenario.actions` outside `network`: bounded fault and route action declarations.

The C++ loader validates references, IPv4 subnet membership, MAC syntax, VLAN
ranges, mirror output ports, router interfaces, and graph-edge path hops. The
`graphx inspect` command prints both the application and infrastructure models.

## Infrastructure lifecycle

P1 resolves logical resources into bounded deterministic names and validates
references, addresses, VLANs and routes. Router interfaces produce one attachment
per interface; an explicit `port` binds to a switch port and its VLAN. Runtime
ownership identities are not generated during normalization.

`infra` and network launchers return `E_PHASE_UNAVAILABLE`, including dry runs.
V3 resource realization is P7 and owned guest realization is P8. The existing
resource modules and ownership store remain covered by isolated, unprivileged
identity and rollback tests. They are not currently wired to authored v3 graphs.

```sh
build/dev/graphx config normalize examples/mixed-network/graphx.yml --target lima
```

Capture declarations name logical mirror attachments with bounded retention,
rotation and file size. Their directory is derived beneath `/var/lib/graphx`.
Faults and manual route actions live under `scenario.actions`; they never run at
baseline or during normalization. Ordered router policy declarations are retained.
