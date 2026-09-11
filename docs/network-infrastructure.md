# Network infrastructure layer

GraphX treats network infrastructure as a peer of logical topology, transport,
deployment, observability, and GUI/control. The versioned `network` section of
`graphx.yaml` owns these objects:

- `networks`: semantic Ethernet, macvlan, or ipvlan address domains realized by OVS;
- `interfaces`: node IP/MAC attachments, separate from GraphX ports;
- `switches`: Open vSwitch bridges, ports, VLAN access/trunk metadata, and mirrors;
- `routers`: Linux namespace or container router interfaces, routes, forwarding,
  and backend-neutral policies;
- `edge_paths`: ordered infrastructure hops for each logical GraphX edge.
- `captures`: bounded Ethernet PCAPNG observers attached to mirror endpoints;
- `faults`: bounded, timed netem profiles attached to realized veth/TAP endpoints.

The C++ loader validates references, IPv4 subnet membership, MAC syntax, VLAN
ranges, mirror output ports, router interfaces, and graph-edge path hops. The
`graphx inspect` command prints both the application and infrastructure models.

## Infrastructure lifecycle

On native Linux, the version-2 `graphx infra create` lifecycle creates and
identity-records OVS bridges/ports, veth or TAP endpoints, router namespaces,
addresses, forwarding, nftables policy, mirrors, bounded capture processes, and
timed qdiscs. Container deployment remains separate. `destroy` verifies exact
kernel, OVS, process, directory, and qdisc identities before changing anything.
`status` reports drift, active capture, and active or expired faults.

Commands are executed without a shell. Review the exact plan on any platform:

```sh
./build/dev/graphx infra create examples/mixed-network/graphx.yaml --dry-run
```

The lifecycle persists an owner-token ledger beneath its state directory and
rolls back partial creates. It intentionally does not reconcile replacement
resources: identity drift fails closed and requires explicit recovery.

Focused examples are available for a single MACVLAN-semantic domain, three
independently routed IPvlan-L2 domains, and three IPvlan-L3 subnet domains. See
`examples/macvlan`, `examples/ipvlan-l2`, and `examples/ipvlan-l3`. These profile
names select validated OVS flow and routing semantics; they do not select Docker
network drivers. Compose is a management plane and GraphX owns each data-plane
veth attachment.

The `examples/static-route-policy` laboratory adds three OVS domains around one
namespace router. A route declared with `install: manual` is validated but
omitted from `infra create`; only that exact declared destination can be changed:

```sh
sudo ./build/dev/graphx infra route apply examples/static-route-policy/graphx.yaml \
  --router route-router --destination 10.64.30.10/32
sudo ./build/dev/graphx infra route clear examples/static-route-policy/graphx.yaml \
  --router route-router --destination 10.64.30.10/32
```

Use `--dry-run` without sudo on any platform for inspection. Runtime route,
policy, OVS, and packet claims require the native-Linux lab procedure.

## Declarative capture and faults

Version-2 captures name a `mirror` attachment and a directory beneath
`/var/lib/graphx/captures`, whose root-owned boundary and contents are VM-local
storage in Lima. Size, file-count,
rotation-time, retention, and snap-length bounds are mandatory and strictly
validated. Each run uses a root-owned mode-0700 owner-specific directory whose
device, inode, UID, GID, and mode are recorded and rechecked. Capture files must
remain root-owned and non-writable by group or other. Destroy stops only the
recorded dumpcap process, removes every write bit from the identity-checked
root-owned capture files, and then seals that session mode 0550; the bounded
evidence is intentionally retained. Expired, unmodified owner-shaped sessions
are pruned on the next create only when every approved file remains root-owned,
single-link, and read-only; unexpected files or identity changes are never
removed as retention cleanup.

Export one complete PCAPNG snapshot without exposing the live directory:

```sh
sudo ./build/dev/graphx infra capture export \
  examples/network-observability/graphx.yaml --capture ethernet-span \
  --output /tmp/ethernet-span.pcapng
```

Export refuses an existing or symlink destination and rejects incomplete,
oversized, replaced, or unhealthy capture state. Ethernet PCAPNG remains a
separate trust domain from application-level GraphX LINKTYPE_USER0 capture.

Declarative faults target a realized `container_veth`, `namespace_veth`, or
`qemu_tap`. The lifecycle records the interface ifindex, exact netem text, and
an identity-checked timer plus the current boot identity and monotonic
application/expiry deadline. Status reports `active` until automatic expiry and
`expired` afterward. A missing timer/qdisc before the recorded deadline, a boot
change, or an unrelated replacement qdisc fails closed.

The earlier imperative `graphx infra fault` command was retired in M8. Faults
must be declared under `network.faults` so duration, interface identity, qdisc
state, timers, rollback, and recovery use the same fail-closed lifecycle.

Each OVS bridge may expose an owned SPAN output. Use the declarative capture
configuration and `infra capture export` rather than attaching an untracked
capture process to the live interface.

See `examples/network-observability` for the M7 declarative form.

## macOS execution model

The privileged macOS execution environment is the dedicated ARM64 Linux Lima
VM described in `infrastructure/lima/README.md`. Rootful Docker, system OVS,
namespaces, veth/TAP, QEMU, ownership ledgers, captures, and faults stay inside
that VM. The source checkout is the only writable host mount. No Docker or OVS
socket is forwarded to macOS.

The Docker Desktop userspace-OVS simulation and its privileged container image
were removed in M8. This avoids presenting an approximate Docker bridge topology
as evidence for the system-OVS data plane. See `docs/m8-compatibility.md`.
