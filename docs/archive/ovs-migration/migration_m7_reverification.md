# Migration M7 independent re-verification

**Candidate:** remediated, uncommitted M7 worktree on verified M6 baseline
`a35cdd705d06c6291602095db8d190a5fd0c5bfe`  
**Verified:** 2026-09-10  
**Environment:** macOS ARM64 host and repository-derived Lima `graphx` guest,
`vz`/AArch64, configuration digest
`1ebd69ae1cb9d4e9524540657d898417abe62f85fa3aab19b074682a99ed1959`  
**Verdict:** **FAIL — the two original blockers are remediated, but retained
capture evidence is not read-only.**

## Finding closure

### M7-V001 — Premature fault loss

**Result:** Remediated.

The live ledger now records the Linux boot ID and monotonic application/expiry
nanoseconds. A verifier-owned 30-second fault was interrupted immediately by
killing its recorded timer and removing its exact qdisc. Before the deadline,
status returned nonzero with `state=missing-or-replaced`, and destroy refused
the state. The normal test independently proved active state, self-expiry,
observable `expired` state, and exact cleanup.

### M7-V002 — Capture permission drift

**Result:** Remediated.

The live ledger records directory device, inode, UID, GID, and mode. A
verifier-owned capture began as root/root mode `0700`. Changing the directory to
`0777` made status, export, and destroy fail closed. Restoring `0700` restored
healthy status. Making a PCAPNG source writable by group/other likewise made
status and export fail. Healthy export remained bounded, complete, Ethernet
PCAPNG, and a destination symlink was refused without creating its target.

## New blocking finding

### M7-RV001 — Retained capture files remain writable

**Severity:** P1 / M7 acceptance blocker  
**Requirements:** M7-004, M7-005, M7-007, M7-010; verifier checks 6 and 10

I independently created and normally destroyed the checked-in network
observability example, then inspected the retained evidence directly. The
result was:

```text
550 root root d .../ethernet-span-<owner-token>
600 root root f .../dumpcap.stderr
600 root root f .../ethernet_00001_<timestamp>.pcapng
```

Changing only the directory to `0550` prevents adding or removing names, but it
does not remove the owner's write permission from the existing files. The
retained PCAPNG contents therefore remain mutable and do not meet the required
read-only evidence boundary.

`stop_capture()` currently stops the exact process and applies `fchmod(0550)`
only to the session directory. It does not identity-check and seal each retained
regular file. Retention pruning also accepts retained files without checking
their root ownership or read-only mode.

**Required remediation:** while holding the verified session directory
descriptor and after stopping dumpcap, enumerate only approved regular,
single-link files with `fstatat(..., AT_SYMLINK_NOFOLLOW)`, require root/root
ownership, open them no-follow relative to that descriptor, and remove all
write bits with `fchmod` before publishing the directory as `0550`. Recheck
file and directory identities after sealing. Retention pruning must accept only
that exact sealed shape. Add a live regression that asserts every retained
PCAPNG and diagnostic file has no write bits and that an unprivileged identity
cannot reopen either for writing.

## Verification matrix

| Check | Result | Evidence |
| --- | --- | --- |
| Fresh quick build/test | Pass | 44/44; `outputs/verification/20260910T225212Z-quick.log` |
| Quality | Pass | formatting, clang-tidy, cppcheck; `outputs/verification/20260910T225241Z-quality.log` |
| Sanitizers | Pass | repository-sanctioned LLVM 21 UBSan; 44 tests plus coverage; `outputs/verification/20260910T225302Z-sanitizers.log` |
| Portable matrix | Pass | C++23/C++20 44/44 each, telemetry 77, web 17 plus production build; `outputs/verification/20260910T225339Z-portable.log` |
| Strict schema/parser and v1 compatibility | Pass | positive and negative M7 contracts, all checked-in configurations, projections, fingerprint/release, and v1 suites passed |
| Lima identity/environment | Pass | `vz` AArch64 and exact digest; evidence `/var/lib/graphx/m1/evidence/20260910T225506Z-38cae80f` |
| M3 through M6 live regressions | Pass | all passed in the candidate Lima guest |
| M7 live lifecycle twice | Pass | each invocation included adversarial drift, injected rollback/crash recovery, and two ordinary cycles; `/var/lib/graphx/m1/evidence/m7-reverification/live-regressions.log` |
| Verifier-owned former-blocker probe | Pass | exact ledger/deadline/mode inspection; early fault loss and directory/source drift refused |
| Mirror/rotation/export/trust-domain behavior | Pass | real OVS/dumpcap, bounded ring, complete Ethernet PCAPNG, exclusive/no-symlink export, separate from USER0 |
| Layered diagnostics | Pass | policy, route, link, attachment, and application evidence passed telemetry/API/web contracts |
| Retained read-only evidence | **Fail** | M7-RV001; directory `0550`, files `0600` |
| Final runtime cleanup | Pass | no GraphX bridge, veth/TAP, namespace, qdisc, ledger, capture process, or timer remained |

## Acceptance decision

The remediation closes M7-V001 and M7-V002, and the rest of the M7 functional
and identity lifecycle remains green. M7 nevertheless fails because retained
capture contents are writable after normal destroy. Remediate M7-RV001, add an
exact retained-file regression, and repeat focused M7 re-verification before
promotion to M8.
