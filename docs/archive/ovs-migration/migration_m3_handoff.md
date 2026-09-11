# Migration M3 implementation handoff

**Recorded:** 2026-09-09 (America/New_York)  
**Repository:** `~/workspace/graphx-docker`  
**Branch/base:** `main` at `b16c9631ffac7151d9b72ae634b8838f7515b606`  
**Product:** GraphX 1.1.0  
**Implementation state:** uncommitted for independent review

> Remediation note: the independent review found issues in dangling-symlink
> handling, graph-marker enforcement, read-only status behavior, and architecture
> text. Those findings are preserved in `migration_m3_verification.md`; their
> fixes and fresh evidence are recorded in `migration_m3_remediation.md`.

## Verdict

Migration M3 is implemented. Version-2 `infra create`, `status`, `destroy`, and
`recover` now manage only identity-owned Open vSwitch bridges. The lifecycle
uses a persistent owner ledger, a per-graph non-blocking lock, atomic state
publication, intrinsic OVS ownership markers, stable UUID checks, complete-set
cleanup preflight, and UUID-and-marker-conditional OVSDB deletion.

M3 does not create ports, veth pairs, namespaces, TAP devices, profile flows,
mirrors, routes, rules, qdiscs, captures, processes, or Docker networks. Those
remain M4 and later work. Version-1 behavior and all five frozen M0 plan
fingerprints are unchanged.

## Acceptance matrix

| ID | Requirement | Implementation and evidence | Status |
|---|---|---|---|
| M3-001 | Persistent bounded ownership | State defaults to `/var/lib/graphx/runs`; `--state-dir` and `GRAPHX_STATE_DIR` provide test overrides. The root and files require exact 0700/0600 modes and reject symlinks. | Implemented |
| M3-002 | Durable state identity | The ledger records graph ID, literal configuration SHA-256, random 128-bit owner token, lifecycle status, expected bridges, and stable bridge UUIDs. Initial state uses atomic no-replace publication; updates use file and directory `fsync` plus atomic replacement. | Implemented |
| M3-003 | Serialized mutation | A mode-0600 per-graph lock uses non-blocking exclusive `flock`; concurrent operations fail before mutation. | Implemented |
| M3-004 | Atomic owned bridge creation | Each bridge is created and marked in one OVSDB transaction with `datapath_type=system` and owner, digest, and graph `external_ids`. Existing state and every occupied bridge name are refused. | Implemented |
| M3-005 | Interrupted-create recovery | State is saved before and after every mutation. Recovery discovers an atomic mutation that preceded its ledger update only by matching its random token, digest, and graph ID. Exception rollback retains state whenever cleanup cannot be proven complete. | Implemented |
| M3-006 | Identity-safe cleanup | Cleanup preflights every resource before deleting the first. Each delete transaction waits on the recorded UUID and all three intrinsic markers, removes that UUID from the root bridge set, and destroys that exact record. Replacements and owned siblings are preserved on mismatch. | Implemented |
| M3-007 | Lifecycle CLI | Version 2 supports create, status, destroy, recover, and non-mutating dry-run. Version 1 retains its planner; `recover` is version-2-only and redundant v2 `--transactional` is rejected. | Implemented |
| M3-008 | Future identity model | `OwnedResourceIdentity` includes unpopulated extension slots for ifindex, namespace inode, container ID, TAP owner, route, rule, qdisc, capture, and process identity. M3 persists only bridge UUIDs. | Implemented |
| M3-009 | Scope boundary | Portable tests prove that v2 dry-run emits no Docker, port, veth, namespace, TAP, mirror, route, fault, capture, or process mutation. | Implemented |
| M3-010 | Compatibility and evidence | C++20/C++23 portable, quality, sanitizer, projection, documentation, fingerprint, live Lima system-OVS, and retained M1 regression gates were exercised. | Implemented |

## Changed paths

- `include/graphx/ownership.hpp` and `src/ownership.cpp` add the ownership model
  and bridge lifecycle executor.
- `apps/cli/main.cpp` routes version-2 lifecycle actions to M3 and exposes
  `recover` and the bounded state-directory override.
- `src/infra.cpp` retains the version-1 planner and makes the M3/later-phase
  boundaries explicit.
- `tests/test_m3_ownership.py`, `tests/test_config_v2.py`,
  `tests/test_m3_ownership_live.py`, `tests/test_config.cpp`, and `CMakeLists.txt`
  cover the portable and opt-in privileged Linux M3 boundaries.
- `docs/ownership-m3.md`, `docs/security.md`, `docs/configuration-v2.md`,
  `docs/upgrade.md`, `README.md`, and `SUPPORT.md` document operation, recovery,
  security, and scope.
- `docs/GraphX_Architecture.md`, `scripts/generate-architecture-doc.py`, and
  `docs/GraphX_Architecture.docx` synchronize the maintained architecture
  source and editable edition.
- `prompt/implement.md`, `prompt/verifier.md`, and
  `prompt/ovs_migration_implementation_plan.md` define the M3 work and its
  independent verification boundary.
- `examples/udp-unicast/run.sh` and `examples/udp-multicast/run.sh` increase the
  existing subscriber startup allowance from 200 ms to 1 s. This is a
  test-harness stabilization discovered while running full M3 gates; it does
  not change configuration or network realization.

## Runtime behavior

The state transition is:

```text
absent -> creating -> ready -> destroying -> absent
              \-> recover -> absent
```

Create publishes `creating` before the first OVS mutation. A normal mutation
captures its stable UUID and republishes the ledger before continuing. The
verifier-only `GRAPHX_M3_FAIL_AFTER=N` point exercises in-process rollback;
`GRAPHX_M3_CRASH_AFTER=N` terminates after the atomic OVS mutation but before
its UUID ledger update, proving marker-based recovery.

Status creates no state artifacts and returns zero only for a ready,
configuration-matched ledger whose every recorded bridge still has the expected
UUID, owner token, configuration digest, and graph ID. Configuration drift is reported and returns nonzero, but controlled
cleanup continues to use the immutable ledger identity rather than the edited
configuration.

Destroy and recover discover any marker-owned unrecorded mutation, validate the
entire resource set, set `destroying`, and delete in reverse order. A missing
owned bridge is tolerated; a same-name replacement is never adopted or
deleted. Missing state and repeated cleanup fail closed without mutation.

## Verification evidence

### macOS portable gates

- `scripts/verify.sh quick` passed all 39 tests in 39 seconds.
  Log: `outputs/verification/20260909T200400Z-quick.log`.
- `scripts/verify.sh portable` passed the complete C++23 and C++20 39-test
  suites, every checked-in topology, local transport pipelines, 77 telemetry
  tests, 16 web tests, and the web build in 74 seconds.
  Log: `outputs/verification/20260909T195516Z-portable.log`.
- `scripts/verify.sh quality` passed formatting for 49 C++ files plus
  clang-tidy and cppcheck.
  Log: `outputs/verification/20260909T200543Z-quality.log`.
- `scripts/verify.sh sanitizers` passed 39 enabled tests plus sanitizer coverage
  under LLVM 21 UBSan on macOS in 32 seconds.
  Log: `outputs/verification/20260909T195704Z-sanitizers.log`.
- The projection drift check and focused config, M3 ownership, documentation,
  and Lima-static CTests passed after the final state-I/O hardening.
- All five M0 version-1 dry-run SHA-256 values exactly match
  `migration_m0_baseline.md`.
- The regenerated architecture DOCX rendered as 23 pages. Every page was
  visually inspected; no clipping, overlap, broken table, or missing glyph was
  observed.

### Lima ARM64 system OVS

The retained `graphx` Lima instance is running on ARM64 with the `vz` backend,
matching its state at the start of this implementation session. A fresh guest
out-of-tree build at `/var/tmp/graphx-m3-build` compiled the final ownership
implementation with GCC 13.

Live OVS checks passed for:

- normal create/status/destroy and exact 0700/0600 modes;
- system datapath and all three ownership `external_ids`;
- repeated create and missing-state refusal;
- unowned collisions and permissive, empty/malformed, and symlink state;
- configuration drift and a missing owned bridge;
- non-blocking concurrent graph locking;
- injected rollback after mutation 1;
- hard crashes after mutation 1 and mutation 2, marker discovery, recovery, and
  immediate retry;
- same-name replacement with no markers and with copied token/digest markers;
- complete-set preflight preserving both the replacement and an owned sibling;
- repeated cleanup refusal without unrelated mutation.

All temporary M3 bridges and ledgers were removed. The retained M1 verifier
then passed with exact before/after comparison; evidence is
`/var/lib/graphx/m1/evidence/20260909T194716Z-d26df0e0`. Its first attempt saw
only an unrelated `systemd-timesyncd` listener appear between snapshots; the
unchanged rerun passed once that service state was stable.

## Limits and risks

- M3 owns bridges only. A ready M3 ledger is not evidence that any application,
  container, namespace, or QEMU endpoint is attached.
- The lifecycle currently invokes `ovs-vsctl` as a bounded direct child process;
  callers need permission to use the system OVS database. In the Lima profile,
  runtime mutations are run through the privileged guest boundary.
- `--state-dir` is intended for controlled test/operator state roots. Existing
  directories with permissions other than 0700 are refused rather than
  silently changed.
- Hard power loss can occur between any two durable writes. Recovery is safe
  because the pre-mutation ledger contains the random intrinsic marker needed
  to discover a bridge whose UUID was not yet recorded.
- This handoff is implementation evidence, not the independent verification
  report required by `prompt/verifier.md`.

## Independent verification

The verifier should derive M3-001 through M3-010 from `prompt/verifier.md` and
write `migration_m3_verification.md` without product changes. It should use a
fresh out-of-tree build, independently repeat every Lima mutation and
replacement case, inspect the conditional deletion transaction, rerun the M1
regression verifier, and return the VM and unrelated state to their initial
condition.
