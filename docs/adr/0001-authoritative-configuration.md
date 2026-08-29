# ADR 0001: Authoritative versioned GraphX configuration

- Status: Accepted
- Date: 2026-08-28

## Context

GraphX previously described its graph in `graphx.yaml`, while each demo process
independently constructed TCP transports from environment variables. The file
was documentation rather than an executable contract, and the logical,
transport, and deployment models could drift.

## Decision

`graphx.yaml` is the authoritative source model. Version 1 is represented by a
typed C++ configuration model and a JSON Schema 2020-12 document. The loader
performs strict semantic validation after YAML parsing and aggregates errors.
Unknown keys in the core `version`, `graph`, `transport`, `network`, and `deployment`
surfaces are rejected. `observability` remains an open extension surface owned
by its later phase. Image and process-command hints live in deployment service
placements, never in logical graph nodes.

Runtime edge transports are created by a factory from validated transport
settings. Deployment metadata never enters `Node`, `Edge`, or `Transport`.
Overrides use dotted paths with precedence:

1. configuration file;
2. `GRAPHX_OVERRIDES` environment variable;
3. explicit CLI `--set` values.

Version 1 permits only directed acyclic graphs because the current startup and
blocking transport model cannot safely schedule feedback cycles. Cycle support
requires an explicit future configuration version or capability.

## Consequences

- Applications and tooling share one validated model.
- Configuration failures occur before transports perform network operations.
- Existing envelope and framing formats are unchanged.
- yaml-cpp 0.9.0 is a pinned, hash-verified build dependency.
- Configuration version changes require schema, compatibility documentation,
  fixtures, and migration guidance.
