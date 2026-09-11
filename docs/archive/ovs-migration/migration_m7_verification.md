# Migration M7 independent verification

**Candidate:** uncommitted M7 worktree on verified M6 baseline
`a35cdd705d06c6291602095db8d190a5fd0c5bfe`  
**Verified:** 2026-09-10  
**Environment:** macOS ARM64 host; Lima `graphx` guest, `vz`/AArch64,
Linux 6.8.0-134, virtiofs source and ext4 runtime storage  
**Verdict:** **PASS — M7 is acceptable for promotion to M8.**

No blocking findings remain. M7-V001, M7-V002, and M7-RV001 were reproduced
from their historical reports, inspected in the remediated implementation, and
closed by fresh checked-in and verifier-owned live evidence.

## Finding closure

| Finding | Independent result | Evidence |
| --- | --- | --- |
| M7-V001: premature fault loss accepted as expiry | Closed | The ledger now records boot identity plus applied and expiry monotonic nanoseconds. The live suite kills the exact timer and removes the qdisc before expiry; status and destroy fail closed. Natural expiry remains healthy and observable. |
| M7-V002: active capture permission drift accepted | Closed | The ledger records directory device, inode, UID, GID, and exact mode `0700`. Status, export, destroy, and recovery reject mode drift and unsafe source-file modes. |
| M7-RV001: retained capture files writable | Closed | Normal destroy produced a root/root session at `0550`; every approved regular single-link file had no write bits, and UID/GID 65534 could not append. Expiry skipped the complete session after one PCAPNG was made writable and removed it after mode restoration. |

## Requirement matrix

| ID | Result | Independent evidence |
| --- | --- | --- |
| M7-001 strict declarations | Pass | Fresh schema/parser tests accept the checked-in v2 example and reject relative/out-of-bound capture storage, non-mirror capture targets, mirror fault targets, zero/excess bounds, and jitter above delay. Version-1 configurations and frozen migration/fingerprint contracts remain green. |
| M7-002 capture ownership | Pass | Live ledger inspection found graph, configuration, owner, directory device/inode/UID/GID/mode, mirror interface/ifindex, dumpcap PID/start time, and M7 phase identities. Active session mode was exactly `0700`. |
| M7-003 bounded Ethernet PCAPNG | Pass | Real dumpcap observed the declared OVS mirror. `capinfos` identified PCAPNG with Ethernet encapsulation; the live suite enforced snap length and bounded file-count/size/time rotation. The plan and docs keep it separate from GraphX LINKTYPE_USER0 application capture. |
| M7-004 read-only retention | Pass | Normal destroy stopped dumpcap, removed every file write bit, and sealed the session at `0550`. Strict expiry required approved root-owned, single-link read-only files. Writable drift preserved the full expired session; restoration enabled bounded pruning. |
| M7-005 safe export | Pass | A verifier-owned live export produced a readable complete snapshot. Existing output and a destination symlink were both refused. Source selection is descriptor-relative, no-follow, identity/mode checked, single-link, bounded, and complete-block validated. |
| M7-006 timed fault | Pass | Live status and `tc` showed active netem. Ledger inspection found interface/ifindex, exact qdisc, timer PID/start time, boot identity, and applied/expiry monotonic deadlines. Natural expiry reported expired and removed netem; premature loss failed closed. |
| M7-007 rollback/recovery | Pass | Both independent M7 invocations included deliberate create failure, hard-crash recovery, permission drift, premature timer/qdisc loss, exact destroy, and retained evidence sealing. No unowned or replaced resource was accepted. |
| M7-008 layered diagnostics | Pass | Fresh telemetry and console suites passed. Policy, route, link, attachment, and application layers retain distinct state/evidence semantics in API derivation, topology data, inspector output, and styling. |
| M7-009 example/docs | Pass | The checked-in network-observability example validated and documented create/status/export/destroy, VM-local storage, retention/pruning, and Ethernet versus USER0 trust domains. Documentation consistency passed. |
| M7-010 regression/cleanup | Pass | M3-M6 privileged regressions passed. M7 ran twice with three lifecycle cycles per invocation. The final audit found no GraphX process, timer, bridge, veth/TAP, namespace, qdisc, capture session, ownership state, or test container residue. |

## Fresh verification evidence

- Quick: 44/44,
  `outputs/verification/20260910T232421Z-quick.log`.
- Quality: formatting, clang-tidy, and cppcheck passed,
  `outputs/verification/20260910T232454Z-quality.log`.
- Sanitizers: all 44 enabled tests plus sanitizer coverage passed under the
  repository-sanctioned macOS LLVM 21 UBSan profile,
  `outputs/verification/20260910T232510Z-sanitizers.log`.
- Portable: C++23 44/44, C++20 44/44, telemetry 77/77, console 17/17, all
  checked-in configurations, examples, secure runtime checks, and production
  console build passed,
  `outputs/verification/20260910T232543Z-portable.log`.
- Authoritative Lima verifier: pass; AArch64 guest, matching mounted source,
  ext4 runtime storage, rootful Docker 29.1.3, Buildx 0.30.1, and OVS 3.3.9,
  `/var/lib/graphx/m1/evidence/20260910T232701Z-9f39ceda`.
- Privileged M3-M7 regressions:
  `/var/lib/graphx/m1/evidence/m7-independent-final/live-regressions.log`.
- Separate verifier-owned ledger/capture/export/retention probe:
  `/var/lib/graphx/m1/evidence/m7-independent-final/verifier-probe.log`.
- `git diff --check`: pass before this report was written.

## Acceptance decision

M7-001 through M7-010 pass. The implementation satisfies the declarative,
bounded, identity-owned capture/fault lifecycle; the three earlier security
findings are closed; prior M3-M6 behavior remains green; and the final runtime
state is clean. M8 may begin after the user accepts this verification result.

No product source fix was made during verification.
