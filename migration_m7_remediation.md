# Migration M7 remediation handoff

**Recorded:** 2026-09-10 (America/New_York)  
**Repository:** `~/workspace/graphx-docker`  
**Source report:** `migration_m7_verification.md`  
**Implementation state:** uncommitted and ready for independent re-verification

## Outcome

Both independent M7 verification blockers are remediated. Timed faults now
carry a boot-bound monotonic deadline that distinguishes legitimate expiry from
premature timer/qdisc loss. Active capture sessions now enforce their recorded
root ownership and exact mode, and capture-file inspection/export is anchored
to the verified directory descriptor.

This handoff does not replace the failed independent verdict. M7 still requires
a separate re-verification before acceptance or any move to M8.

## Finding closure

| Finding | Remediation | Regression | State |
| --- | --- | --- | --- |
| M7-V001: premature fault loss reported as expiry | The ledger records Linux boot ID plus monotonic application and expiry nanoseconds. An absent timer/qdisc is healthy only on the same boot after the exact deadline; early disappearance and boot changes fail closed. | The live test kills the recorded timer and removes its exact qdisc during a 2-second declaration, then requires both status and destroy to refuse the state. Ordinary automatic expiry still reports `expired` and cleans exactly. | Remediated |
| M7-V002: capture permission drift accepted | The ledger records directory UID, GID, and mode in addition to device/inode. Status, export, destroy, and recovery require root/root mode `0700`. PCAPNG enumeration and export use `openat`/`fstatat` beneath a verified no-follow directory descriptor and require root-owned, single-link files not writable by group/other. Final sealing uses `fchmod` on that same descriptor and verifies the published path as root/root mode `0550`. | The live test changes an active directory to `0777` and separately makes a capture source writable; status/export/destroy must fail. Restoring the exact identities returns the lifecycle to healthy. | Remediated |

## Implementation surface

- `src/ownership.cpp`: extended capture/fault ledger identities, strict ledger
  loading, deadline-aware fault health and cleanup, descriptor-anchored capture
  inspection/export, and exact active/retained directory modes.
- `tests/test_m7_network_observability_live.py`: adversarial early-expiry,
  directory-mode, and capture-file-mode regressions plus ledger assertions.
- `tests/test_m7_network_observability.py`: portable source-contract coverage
  for the new identities and fail-closed helpers.
- `docs/network-infrastructure.md`, `docs/test-reference.md`, and
  `examples/network-observability/README.md`: updated operational and security
  contracts.
- `migration_m7_handoff.md`: corrected M7 ownership and verification claims.

## Validation evidence

- Final quick profile: 44/44 passed;
  `outputs/verification/20260910T224633Z-quick.log`.
- Final quality profile: formatting, clang-tidy, and cppcheck passed;
  `outputs/verification/20260910T224703Z-quality.log`.
- Final sanitizer profile: 44 tests plus sanitizer coverage passed under the
  repository's macOS LLVM 21 UBSan policy;
  `outputs/verification/20260910T224723Z-sanitizers.log`.
- Final portable profile: C++23 and C++20 passed 44/44 each, telemetry passed
  77 tests, web passed 17 tests and its production build;
  `outputs/verification/20260910T224803Z-portable.log`.
- M3, M4, M5, and M6 privileged live regressions passed in the ARM64 Lima VM.
- The finalized M7 privileged test passed in two separate invocations. Each
  invocation includes the new adversarial checks, injected rollback and hard
  crash recovery, two normal create/destroy cycles, real OVS/dumpcap/PCAPNG,
  genuine timed expiry, and exact cleanup.
- The authoritative Lima environment verifier passed with evidence at
  `/var/lib/graphx/m1/evidence/20260910T224243Z-ef1a41aa`.
- `git diff --check` passed.

## Re-verification focus

Rebuild from the candidate and repeat both early-disappearance and capture-mode
mutations independently. Inspect the ledger for directory UID/GID/mode, boot
ID, and monotonic application/expiry values. Confirm status, export, destroy,
and recovery fail closed while identities are altered, then restore or clean
only the exact test objects and prove ordinary expiry, export, retention, and
zero runtime residue. Retain `migration_m7_verification.md` unchanged and issue
a separate M7 re-verification verdict.
