# ADR 0019: Versioned normalized configuration contract

- Status: Accepted for simplification S1
- Date: 2026-09-11

## Context

The C++ runtime and telemetry service both consume `graphx.yaml`, but they use
different parsers and construct their own resolved topology. JSON Schema tests
limit drift at the input boundary without providing downstream consumers one
authoritative, defaulted representation. Moving configuration interpretation
between processes without a versioned contract would replace parser drift with
an implicit deployment dependency.

## Decision

The C++ configuration loader remains authoritative and can project a validated
`GraphConfig` as deterministic JSON through `graphx config normalize`. The JSON
has its own `contract_version`, independent of the source YAML version, and a
strict installed schema named `normalized-graph-v1.schema.json`.

Normalization applies runtime precedence in the order `file <
GRAPHX_OVERRIDES < --set`. It emits resolved defaults and the complete graph,
transport, network, deployment, and observability model. It never reads
credential files or copies credential environment variables. Configuration
paths, including TLS certificate and private-key paths, remain metadata; file
contents do not. The top-level source `version` is immutable and cannot be
changed by either override layer.

Version-2 input is marked mutable with the sole `ovs` backend. Version-1 input
may be normalized for compatible inspection, but is marked non-mutable and
uses a `compatibility-only` network label. A legacy Docker driver may be
retained as migration metadata but is never advertised as an active backend.

An incompatible JSON shape requires a new contract version and schema. Source
collection order is retained, object key order is fixed, numeric formatting is
locale-independent, and output has exactly one final newline.

## Consequences

- Downstream consumers can migrate from YAML parsing to one resolved contract
  without changing the user-facing configuration format.
- S1 introduces the projection and schema; changing telemetry startup and
  packaging remains a separate S2 deployment change.
- Contract fixtures make semantic and serialization changes explicit during
  review.
- Version-1 normalization does not weaken the M8 pre-mutation refusal.
- Adding a configuration field requires updating the C++ model, normalized
  projection, schema, and contract tests together.
