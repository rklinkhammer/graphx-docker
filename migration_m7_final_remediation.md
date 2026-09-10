# Migration M7 retained-evidence remediation handoff

**Recorded:** 2026-09-10 (America/New_York)  
**Repository:** `~/workspace/graphx-docker`  
**Source report:** `migration_m7_reverification.md`  
**Implementation state:** uncommitted and awaiting independent re-verification

## Outcome

M7-RV001 is remediated. Normal capture shutdown now seals the contents as well
as the directory, so retained PCAPNG and dumpcap diagnostics have no write bits.
Retention expiry accepts only the same root-owned, single-link, approved,
read-only file shape.

This implementation handoff does not change the failed verdict in
`migration_m7_reverification.md` and does not authorize M8.

## Finding closure

| Finding | Remediation | Regression | State |
| --- | --- | --- | --- |
| M7-RV001: retained capture files remained writable | After stopping the exact dumpcap process, GraphX enumerates the complete approved file set through the verified session directory descriptor. Each file is opened with `openat(..., O_NOFOLLOW)`, matched by device/inode, required to be root/root and single-link, stripped of all write bits with `fchmod`, and rechecked before the directory is changed to `0550`. Expiry pruning uses descriptor-relative identity checks and accepts only that sealed shape. | Every normal live-test cycle asserts directory mode `0550`, root/root regular approved files with no write bits, an unprivileged write refusal, successful expiry pruning, and final zero residue. | Remediated |

## Implementation surface

- `src/ownership.cpp`: approved-file enumeration, two-stage descriptor-relative
  sealing, retained-file identity validation, and strict expiry pruning.
- `tests/test_m7_network_observability_live.py`: exact retained modes,
  ownership, approved names, PCAPNG presence, and unprivileged write refusal.
- `tests/test_m7_network_observability.py`: portable remediation markers.
- `docs/network-infrastructure.md`, `docs/test-reference.md`,
  `examples/network-observability/README.md`, and `migration_m7_handoff.md`:
  corrected retained-evidence contract.

## Validation evidence

- Host quick gate: PASS (44/44),
  `outputs/verification/20260910T231842Z-quick.log`.
- Host quality gate: PASS,
  `outputs/verification/20260910T231910Z-quality.log`.
- Host sanitizer gate: PASS (44/44 enabled tests plus sanitizer coverage),
  `outputs/verification/20260910T231929Z-sanitizers.log`.
- Host portable gate: PASS for C++23 and C++20 (44/44 each), telemetry
  (77/77), console (17/17), and production build,
  `outputs/verification/20260910T232005Z-portable.log`.
- Authoritative Lima verification: PASS,
  `/var/lib/graphx/m1/evidence/20260910T231351Z-5a314a50`.
- Privileged M3-M6 regression rerun: PASS,
  `/var/lib/graphx/m1/evidence/m7-final-remediation/prior-milestone-regressions.log`.
- Strengthened M7 live suite: PASS twice, with three lifecycle cycles per
  invocation, including writable-retained-file pruning refusal and recovery,
  `/var/lib/graphx/m1/evidence/m7-final-remediation/live-regressions.log`.
- Final residue audit: no dumpcap/timer processes, GraphX OVS bridges,
  namespaces, M7 links, netem qdiscs, capture sessions, or test containers.

## Re-verification focus

Create and normally destroy a real dumpcap-backed capture, then independently
inspect every retained file. Require root/root, one link, an approved name, and
no owner/group/other write bits; verify an unprivileged write attempt fails.
Alter one expired retained file back to writable and prove retention pruning
leaves the entire session untouched. Restore the exact mode, prove bounded
expiry removes it, and repeat the complete M7 lifecycle and residue audit.
