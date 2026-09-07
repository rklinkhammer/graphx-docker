# ADR 0015: Explicit manual route activation

- Status: Accepted for Phase 14 implementation
- Date: 2026-09-06

## Context

GraphX route definitions were previously installed unconditionally during
infrastructure creation. The static-route teaching laboratory needs one route
to be declared and validated while remaining absent initially. Accepting an
arbitrary route from a script or web API would bypass the authoritative model
and broaden a privileged mutation surface.

## Decision

Configuration version 1 gains an optional route `install` value. `create` is
the default and preserves existing behavior. `manual` omits that route from the
create plan. `graphx infra route apply|clear` requires a router ID and exact
destination already present in the validated configuration; apply uses the
declared next hop and device, and clear deletes only that destination.

Native route mutation remains Linux-only. Other platforms may use `--dry-run`
to inspect the exact command. The Phase 14 GUI consumes a separate, bounded,
strict evidence projection and does not receive general route privileges.

## Consequences

- Existing version-1 files retain create-time route behavior.
- Route transitions are reviewable, deterministic, and constrained to declared
  destinations.
- The focused lab can distinguish a missing route from a policy denial without
  introducing application-level failure semantics.
- This is activation, not reconciliation. GraphX still does not continuously
  converge or assume ownership of arbitrary host routes.
- Native Linux verification must prove route, packet, nftables, OVS, capture,
  lifecycle, and unrelated-host-state behavior.

## Alternatives considered

- Install and remove the route directly in the example script: rejected because
  the active route would not be constrained by the authoritative model.
- Add an unrestricted route API to telemetry: rejected because a teaching
  transition does not justify a broad privileged network control plane.
- Use `tc netem` or application drops to simulate failure: rejected because
  neither proves route-table behavior.
