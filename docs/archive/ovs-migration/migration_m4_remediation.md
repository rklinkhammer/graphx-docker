# Migration M4 remediation

**Remediated:** 2026-09-09 (America/New_York)  
**Prior verdict:** `migration_m4_verification.md` — CHANGES REQUIRED  
**Prior report SHA-256:** `0954cc10888a542b0b38941a16f989ff5dca2aab6cce6524684f0e1a6730f0d0`

## Scope

This remediation addresses only the two findings from independent M4
verification. It does not advance to M5 or implement namespace veth, TAP,
profile flows, mirrors, general policy, faults, or capture realization.

## P1 replacement preservation

Endpoint cleanup now resolves the expected OVS Port and Interface names and
compares their UUIDs with the ledger during complete-set preflight. It repeats
the comparison immediately before endpoint deletion. A same-named replacement
therefore fails closed even when it copies every GraphX external ID.

The ledger now records the implicit internal Port UUID created with each OVS
bridge. Owned bridge deletion places the exact final port set containing only
that internal Port in the same OVSDB transaction as the bridge UUID, ownership,
configuration, graph, and destruction conditions. This prevents a late or
unrecognized Port from being removed as a child of the bridge without blocking
normal bridge cleanup.

The privileged regression creates and preserves three adversarial forms:

1. a replacement Port and Interface with the same name, copied markers, and a
   changed Port UUID; and
2. a replacement Interface with the same name and copied markers, attached to
   the recorded Port but carrying a changed Interface UUID; and
3. a replacement host link with the same name and copied alias but a changed
   kernel ifindex.

All three cases require nonzero destroy and preserve the replacement, bridge,
and ownership ledger; the OVS cases also preserve the recorded host veth.
Test-owned resources are then removed directly before the next isolated case.

## Remediation evidence

- The macOS development and quality profiles each pass all 40 tests.
- The sanitizer profile passes all 40 enabled tests plus its coverage gate; its
  package test remains intentionally disabled by that profile.
- All five frozen M0 version-1 dry-run SHA-256 values remain exact.
- ARM64 Lima passes the expanded M4 live regression and retained M3 live
  ownership regression with system OVS and rootful Docker.
- The M4 live regression covers normal create/status/destroy, container restart
  detection and reattachment, all three replacement forms, injected rollback,
  hard-crash recovery, and a final clean create/destroy cycle.
- The disposable container, bridge, host link, and `graphx-m4-test:local` image
  were removed and absence was checked after the run.

## P2 architecture status

The architecture achievement table now identifies M3 owned bridges and M4
managed-container veth endpoints. It explicitly defers namespace/TAP endpoints
and profile flows. The editable DOCX is regenerated from the corrected Markdown
and all 24 rendered pages were visually inspected.

## Required re-verification

Repeat the two copied-marker replacement cases plus host-ifindex replacement,
normal cleanup after restoration, failure/crash recovery, fresh portable,
quality, sanitizer, fingerprint, M2/M3 regression, documentation, DOCX render,
and final Lima cleanup gates. M4 remains unaccepted until that independent
re-verification passes.
