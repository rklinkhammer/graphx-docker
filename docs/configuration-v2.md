# GraphX configuration version 2

Configuration version 2 is the explicit semantic boundary for the OVS
migration. Version 1 remains accepted with its original Docker
`bridge`/`macvlan`/`ipvlan` driver meaning. Version 2 never accepts `driver`,
`parent`, `mode`, `network.interfaces`, or `deployment.network`; it uses
semantic profiles and explicit attachments instead.

M2 defines and validates intent. M3 adds identity-safe OVS bridge lifecycle
operations for version 2; route, fault, and endpoint realization remain
deferred. A version-2 file is never routed through the legacy Docker network
planner.

## Semantic profiles

Every profile has one fixed, inspectable behavior contract. `management` is
always `separate`: Compose may provide management connectivity, but it never
owns the version-2 GraphX data plane.

| Profile | MAC identity | Learning | Filtering | ARP | Broadcast | Multicast | Routing | Isolation | Management |
|---|---|---|---|---|---|---|---|---|---|
| `ethernet` | endpoint | dynamic | OVS | endpoint | flood | flood | L2 | none | separate |
| `macvlan` | endpoint | dynamic | OVS | endpoint | flood | flood | L2 | host | separate |
| `ipvlan-l2` | shared uplink | suppressed | OVS | endpoint, shared MAC | flood | flood | L2 | host | separate |
| `ipvlan-l3` | shared uplink | none | route | suppressed | suppressed | suppressed | L3 | endpoint | separate |
| `ipvlan-l3s` | shared uplink | none | source validated | suppressed | suppressed | suppressed | source-validated L3 | endpoint | separate |

These values are observable acceptance requirements for later packet tests;
the names do not select Linux Docker plugins. Non-`ethernet` profiles require
an `uplink`. `ethernet`, `macvlan`, and `ipvlan-l2` require exactly one subnet
and a gateway. The routed profiles accept 1–16 subnets and make the gateway
optional.

```yaml
version: 2
network:
  networks:
    - id: sensor-domain
      profile: ipvlan-l3s
      subnets: [10.42.1.0/24, 10.42.2.0/24]
      uplink: sensor-uplink
      external: true
```

`external` records external-reachability intent. It does not transfer lifecycle
ownership to Docker and does not change the OVS-only backend.

## Attachments

`network.attachments` is a sequence with globally unique IDs. Its supported
kinds are:

- `container_veth`: a deployed non-QEMU node, attached later through an owned
  veth pair;
- `namespace_veth`: a Linux namespace router; address, interface, peer, and
  switch are required;
- `qemu_tap`: a node whose runtime is `qemu`; TAP creation is delivered in M6;
- `external`: a graph node whose endpoint lifecycle is outside the generic
  attachment pipeline;
- `mirror`: an OVS switch observation port. It has no network, address, MAC, or
  peer and requires an interface and switch.

Non-mirror attachments require a known semantic network. Addresses must be
IPv4 CIDRs inside that network's declared subnets. MAC addresses use the
canonical six-octet representation.

```yaml
network:
  attachments:
    - id: transform-data
      kind: container_veth
      owner: transform
      network: sensor-domain
      address: 10.42.1.20/24
      mac: "02:42:00:00:01:20"
```

## Deterministic migration

Preview a migration on standard output:

```bash
graphx config migrate graphx.yaml
```

Or create a new file:

```bash
graphx config migrate graphx.yaml --output graphx-v2.yaml
graphx validate graphx-v2.yaml
graphx inspect graphx-v2.yaml
```

The command first validates the literal file with version-1 semantics, never
modifies it, refuses an existing output (including symlinks), and emits
byte-identical output for the same input. Runtime `GRAPHX_OVERRIDES` values are
deliberately ignored: migration is a source-to-source operation, not a runtime
configuration load. Mappings are:

| Version 1 | Version 2 |
|---|---|
| `driver: bridge` | `profile: ethernet` |
| `driver: macvlan` | `profile: macvlan` |
| `driver: ipvlan`, `mode: l2` | `profile: ipvlan-l2` |
| `driver: ipvlan`, `mode: l3` | `profile: ipvlan-l3` |
| `driver: ipvlan`, `mode: l3s` | `profile: ipvlan-l3s` |
| `parent` | `uplink` |
| owner entry in `network.interfaces` | typed entry in `network.attachments` |
| Linux namespace router interface | `namespace_veth` attachment |
| OVS mirror output | `mirror` attachment |
| QEMU node interface | `qemu_tap` attachment intent |
| `deployment.network` | removed; management and data-plane membership are separate |

A deployment service maps to `container_veth`; QEMU maps to `qemu_tap`; other
node interfaces map to `external`. Unsupported or ambiguous legacy fields,
non-system OVS switches, oversized generated IDs, and attachment-ID collisions
fail with a diagnostic instead of guessing.

The migration does not change the source file or claim that M4–M6 realization
exists. Review the resulting profiles and attachment kinds before adopting it.
