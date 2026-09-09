# ADR 0017: OVS is the single backend for semantic network profiles

- Status: Accepted for the OVS migration
- Date: 2026-09-08

## Context

Configuration version 1 uses `network.networks[].driver` to select a Docker
bridge, macvlan, or ipvlan network. The infrastructure planner creates that
Docker network and examples use Compose membership to attach containers. OVS is
modeled beside those Docker networks for selected routing, VLAN, mirror, and
fault laboratories; it is not the universal endpoint switch. Node interface
declarations are validated and displayed, but the generic planner does not
realize container endpoints.

The target architecture needs one controllable data plane for container veth
ports, namespace-router veth ports, QEMU TAP ports, VLANs, observation, and
fault injection. Retaining Docker network drivers as alternate backends would
preserve divergent lifecycle and packet behavior.

## Decision

Open vSwitch with the Linux system datapath is the only GraphX-managed network
backend in the new configuration model.

- Managed containers attach to OVS through owned veth pairs.
- Linux router and diagnostic namespaces attach through owned veth pairs.
- QEMU guests attach through owned TAP devices; QEMU should remain non-root
  where practical.
- OVS ports and flows implement switching, VLAN, isolation, mirror, and profile
  behavior.
- Docker Compose manages process/container lifetime and an optional separate
  management network. It does not own the GraphX data plane.

The terms `macvlan`, `ipvlan-l2`, `ipvlan-l3`, and `ipvlan-l3s` become semantic
profiles. Each profile must specify observable MAC identity, learning and
filtering, ARP, broadcast/multicast, routing, endpoint isolation, and management
reachability before it is accepted. A profile does not select a Docker network
plugin.

The new meaning is represented by configuration version 2. Version 1 retains
its existing meaning as migration input and, during a bounded compatibility
window, as a legacy execution surface. A deterministic migration command will
translate supported version-1 configurations to version 2. The implementation
must not silently reinterpret a version-1 `driver` value as a semantic profile.

Infrastructure realization will be separated into model-independent planning,
OVS realization, endpoint attachment, and persistent ownership state. This is
an internal responsibility boundary, not a speculative multi-backend plugin
interface.

## Consequences

- OVS becomes the common point for container, router, external, capture, fault,
  and future QEMU paths.
- Docker macvlan/ipvlan creation and Compose data-plane membership are removed
  from version-2 plans.
- Version 2 is an intentional compatibility event; version-1 parsing and
  migration require independent regression coverage.
- Semantic fidelity must be demonstrated with packets and realized state, not
  inferred from profile names.
- Container restart, PID reuse, namespace replacement, TAP ownership, and
  management/data-plane separation become explicit lifecycle concerns.
- The existing graph, transport, deployment, observability, OVS switch/router,
  route, policy, mirror, VLAN, edge-path, capture, telemetry, and GUI concepts
  remain reusable.

## Alternatives considered

- Reinterpret version-1 `driver`: rejected because the same file would cause a
  materially different privileged realization without an explicit version
  boundary.
- Keep Docker bridge/macvlan/ipvlan as selectable backends: rejected because it
  defeats the single data-plane and lifecycle objective.
- Introduce a public backend interface immediately: rejected because there is
  one intended backend and no second implementation from which to derive a
  stable abstraction.

