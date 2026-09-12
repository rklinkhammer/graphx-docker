# ADR 0020: Versioned capability-based ownership ledger

- Status: Accepted
- Date: 2026-09-11

## Context

The infrastructure ledger classified a run by the delivery milestone in which
its most advanced resource type was introduced. That classification did not
control behavior: cleanup and status already derive authority from the recorded
resource identities. It also required repeated milestone-specific test hooks
inside one transaction.

The ledger is safety-critical persistent state. Existing version-1 ledgers must
remain readable so GraphX can verify and remove resources it previously created.

## Decision

New ownership ledgers use format version 2 and record graph, configuration,
owner, status, expected capabilities, and stable resource identities. They do
not record a delivery phase.

The loader continues to accept version-1 ledgers only when their historical
phase value and all identity fields are valid. The phase is ignored after
validation. Cleanup authority remains based exclusively on stable resource
identity; names alone are never authority.

Transaction interruption tests use the single bounded hooks
`GRAPHX_TEST_FAIL_AFTER_MUTATION` and `GRAPHX_TEST_CRASH_AFTER_MUTATION`.
Historical fault-timer process names remain recognized for safe cleanup, while
new timers use a capability name.

## Consequences

- Current runtime state describes owned resources rather than project history.
- Every retained version-1 fixture is loaded by the ownership-state test.
- A future incompatible ledger change requires another format version and a
  backward-compatible cleanup path for every still-supported ledger.
- The identity, collision-refusal, locking, transaction, and exact-cleanup
  guarantees are unchanged.
