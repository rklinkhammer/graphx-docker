# Historical plans and verification archive

This directory preserves completed and superseded project records without
leaving them in the repository root or presenting them as current instructions.
Archived files are evidence, not normative guidance. Use
[`../project-decisions.md`](../project-decisions.md), maintained feature docs,
tests, and accepted ADRs when implementing new work.

## Contents

| Directory | Contents | Current interpretation |
| --- | --- | --- |
| [`legacy-phases/`](legacy-phases/) | Feature-phase 2–14 handoffs and verification reports, the 2026-09-09 status snapshot, documentation-consistency handoff, and the original Phase 1–10 runbooks | Phases 3–13 reached accepted historical reports. Phase 1 lacked a report, Phase 2 lacked an independent report, and the retained Phase 14 report ended with changes required. Later M0–M8 work supersedes its networking realization guidance. |
| [`ovs-migration/`](ovs-migration/) | M0 baseline plus M1–M8 implementation, remediation, and independent verification records | The migration completed with the M8 independent re-verification PASS dated 2026-09-10. Earlier failed reports remain intentionally preserved and are superseded by their later remediation/final-verification records. |
| [`work-packages/`](work-packages/) | Completed implementation plans and verifier prompts | Historical scope only. Do not execute these as current assignments. |

## Final migration record

The terminal network-migration evidence is
[`ovs-migration/migration_m8_reverification.md`](ovs-migration/migration_m8_reverification.md).
It records the accepted boundary: version 2 only for mutable infrastructure,
system OVS as the sole backend, veth for containers, TAP for QEMU, declarative
capture/fault ownership, and exact cleanup on Apple Silicon plus Lima ARM64.

Some archived documents contain original relative paths and platform names.
Those strings are retained as historical evidence and must not be copied into
new procedures without checking the current decision guide.

