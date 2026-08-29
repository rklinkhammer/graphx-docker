# ADR 0002: Network infrastructure is a peer layer

- Status: Accepted
- Date: 2026-08-29

## Context

Compose network membership answers which containers may communicate, but it does
not represent a logical GraphX edge, transport semantics, L2 switching, L3
routing, policy, inspection, or impairment. Treating Compose as authoritative
would also couple infrastructure lifetime to one deployment project.

## Decision

Add a strict `network` surface to the version-1 authoritative configuration. It
contains network, interface, switch, router, route/policy, mirror, VLAN metadata,
and logical-edge path abstractions. Native Linux infrastructure is managed by a
separate `graphx infra` lifecycle. Compose consumes external networks.

Use a native Linux namespace router and OVS system datapath for the exact mixed
macvlan/ipvlan reference. Provide a distinct Docker Desktop profile using a
containerized OVS userspace datapath and two bridge domains; label it as a
simulation instead of claiming macvlan equivalence.

## Consequences

- Application topology and network-path topology can be displayed and inspected
  independently while retaining edge-to-path correlation.
- Infrastructure can survive Compose project restarts.
- Full L2 behavior requires native Linux.
- The macOS profile can exercise OVS, routing, mirrors, nftables, and netem but
  cannot reproduce Docker macvlan/ipvlan semantics.
