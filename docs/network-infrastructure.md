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

The compiled runner projects this resolved model into the existing resource
modules and the graph's common ownership ledger. Inspect it without privileges:

```sh
build/dev/graphx run plan --output "$GX_OUTPUT" --state-root "$GX_STATE"
```

OVS execution requires `run up|status|down --allow-privileged` on an authorized
local Linux engine. It is implemented with [P7 Lima acceptance evidence](../design/graph-generation/p7-verification.md).
The legacy `infra` CLI and per-profile shell launchers remain explicitly gated;
wrappers are removed only after their profile passes live acceptance. Guest boot
and scenario actions remain gated. Physical external uplinks require a separate
ownership contract and are not attached implicitly.

```sh
build/dev/graphx config normalize examples/mixed-network/graphx.yml --target lima
```

Capture declarations name logical mirror attachments with bounded retention,
rotation and file size. Their directory is derived beneath `/var/lib/graphx`.
Faults and manual route actions live under `scenario.actions`; they never run at
baseline or during normalization. Ordered router policy declarations are retained.

The runner holds application containers before exec while it prepares veths and
management ACLs, then waits for application listeners before releasing traffic.
Management permits only the resolved platform console and telemetry ports;
forwarding and other management traffic are denied. Namespace diagnostics have
no management route. Router interfaces are projected once, aliases are installed,
and manual routes remain deferred. Capture snapshots use the same ownership
record as processes and infrastructure; the platform mounts only sealed snapshots.
See [execution](execution.md) for release and recovery requirements.
