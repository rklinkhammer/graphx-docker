# ADR 0004: Version 2 envelopes and explicit identities

- Status: accepted
- Date: 2026-08-31

## Context

The original version-1 envelope had a sequence and free-form trace string, but
no stable logical-message identity or causal parent. A sequence is normally
local to a producer, while a trace groups multiple operations. Neither safely
supports duplicate suppression after the TCP transport's intentional complete-
frame retry. Changing version 1 in place would make existing captures and peers
ambiguous.

## Decision

Keep the `GXE` magic and introduce envelope wire version 2. Encode message,
trace, and optional parent-message identities as fixed 128-bit fields. Use a
canonical non-zero lowercase hexadecimal representation in the API. New roots
emit v2; the decoder accepts v1 and v2; v1 values can be forwarded unchanged.
Reject silent v2-to-v1 identity loss. Define exact field order, bounds, malformed
input behavior, golden vectors, and a downstream-first rollout in
[`docs/protocol.md`](../protocol.md).

Ordinary transformation and transport retry retain `message_id`. The explicit
`Envelope::derive` operation creates a new message in the same trace and records
causal lineage. OTLP export maps a canonical GraphX trace ID directly to the OTLP
trace ID and generates each 64-bit span ID directly from operating-system entropy,
including after a prefork lifecycle. Full OpenTelemetry propagation remains out
of scope until Phase 6.

## Alternatives considered

- Reuse sequence as identity: rejected because sequences can collide across
  producers and deployments and describe ordering rather than global identity.
- Reuse trace ID as message identity: rejected because a trace intentionally
  contains multiple messages.
- Add fields to v1: rejected because old readers would misparse the bytes.
- Encode UUID text: rejected because fixed 16-byte fields are smaller and remove
  spelling variants; the API can still expose readable hexadecimal.
- Silently downgrade v2: rejected because losing identity breaks retry and
  deduplication semantics.
- Implement W3C trace context now: deferred to Phase 6 because span-parent and
  sampling propagation require a broader observability contract.

## Consequences

Existing v1 recordings remain readable and byte-stable. New producers require
v2-aware consumers, so deployments must upgrade downstream first. The public
aggregate defaults to v1 to avoid silently changing legacy aggregate-initialized
objects, while factory-created messages use v2. Identity generation contains
process-global entropy/counter state but no vendor or deployment dependency.
Identifiers must never be treated as authentication material.
