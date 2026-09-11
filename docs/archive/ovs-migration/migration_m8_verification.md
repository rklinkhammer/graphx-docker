# Migration M8 independent verification

Date: 2026-09-10 (America/New_York)  
Candidate: working tree based on `e747adbea15a8d0b399f72ea13d631e71faf6d2b`  
Verdict: **FAIL**

M8's focused compatibility implementation works, but the repository does not
yet satisfy compatibility/release closure. The active native-Linux acceptance
launcher and several operator documents still reference M8-deleted launchers,
and the checked-in Lima provisioning installs a Node runtime older than the
repository's supported telemetry runtime.

## Blocking findings

### M8-V01 — active native-Linux acceptance invokes deleted M8 paths (P1)

`scripts/test-features.sh:473-480` special-cases `mixed-network` and invokes:

- `examples/mixed-network/scripts/linux-up.sh`
- `examples/mixed-network/scripts/fault.sh`
- `examples/mixed-network/scripts/linux-down.sh`

All three paths are deleted by the M8 candidate. `scripts/verify.sh:248` still
uses this launcher for the documented `native-linux` verification profile.
Consequently, on a conforming Linux toolchain the release gate will terminate
with a missing-file error after its portable preamble. This violates required
check 5 (no active launcher realizes or selects the retired path) and prevents
the repository-wide native-Linux acceptance gate from completing.

Required remediation: make the loop use the canonical `up.sh`, `status.sh`, and
`down.sh` entry points for all four migrated labs. Do not restore the imperative
`fault.sh`; bounded fault coverage belongs to the declarative M7 lifecycle.
Add a static assertion that active verification scripts contain none of the
removed launcher names.

### M8-V02 — active documentation still instructs use of deleted launchers (P1)

The following current operator documentation names removed entry points:

- `docs/graphical-examples-guide.md:441` — `linux-down.sh` / `macos-down.sh`
- `docs/test-reference.md:631` — `linux-down.sh` / `macos-down.sh`
- `docs/test-procedure.md:303-304` — `linux-down.sh`

The first item is in the migrated mixed-network workflow. These instructions
contradict M8's canonical single-launcher model and independently violate
required check 5. The existing documentation-consistency test passes despite
the stale references.

Required remediation: replace the stale instructions with the canonical
`scripts/down.sh` wording, retaining `down-native-linux.sh` only where it names
the still-supported UDP broadcast helper. Extend documentation consistency
coverage to reject the removed M8 path vocabulary outside historical ADRs and
migration records.

### M8-V03 — the provided Lima runtime cannot run the supported telemetry suite (P1)

`infrastructure/lima/provision.sh:14` installs Ubuntu's unversioned `nodejs` and
`npm` packages. The verified guest has Node.js `v18.19.1`; `SUPPORT.md:22`
defines Node.js 22 as the production telemetry runtime, and the telemetry suite
uses `node:sqlite`.

A supplemental run of the documented Linux acceptance entry point failed in
the telemetry stage: 61/65 JavaScript tests passed, three control/history tests
failed, and `history.test.mjs` could not import `node:sqlite`. It therefore did
not reach M8-V01 dynamically. Host telemetry on Node.js `v26.8.1` passed, and
the focused Lima M3-M8 tests do not depend on Node; neither fact makes the
checked-in Linux runtime suitable for the repository-wide gate.

Required remediation: provision a supported pinned Node.js release (at least
22; preferably the documented CI release), verify `node:sqlite` during Lima
provisioning, and assert the runtime version in `infrastructure/lima/verify.sh`.

## Requirement results

| Requirement | Result | Independent evidence |
| --- | --- | --- |
| M8-001 fresh build and repository gates | **FAIL** | Host `quick`, `quality`, `sanitizers`, and `portable` passed; a separate Lima Debug build succeeded. The documented Linux acceptance entry point fails under the provisioned Node 18 runtime. |
| M8-002 v1 read/project/migrate; infra refusal before mutation | PASS | `graphx-m8-compatibility-closure` and the direct portable closure test passed; post-test state remained empty. |
| M8-003 no Docker-network realization or legacy imperative planner | PASS | Production scan found no Docker network create/remove/inspect or `netem_command`/`InfraAction`; only the generic owned `execute_infrastructure_plan` executor remains. |
| M8-004 canonical labs are v2, OVS-only, management-only Compose | PASS | Focused static test passed; all five canonical Compose files contain no macvlan/ipvlan driver or external data network. |
| M8-005 v1 inputs fixture-only; no active legacy launchers/docs | **FAIL** | M8-V01 and M8-V02. Seven representative v1 fixtures are correctly isolated under `examples/compatibility/v1`, but active references remain. |
| M8-006 Docker Desktop/userspace-OVS simulation absent | PASS | Focused static closure test passed; retired `docker/ovs` and simulation launchers/manifests are deleted. |
| M8-007 QEMU defaults to TAP/OVS | PASS | Root `examples/qemu-node/scripts/demo.sh` selects the TAP/OVS lab. Retained external/container usernet profiles are explicitly deprecated and not implicitly selected. |
| M8-008 Lima M3-M7 plus M8 twice | PASS | Fresh guest build; `graphx-m[3-8].*-live` passed 6/6. M8 then passed in two additional independent invocations. |
| M8-009 compatibility, rollback, storage, platform matrix | PASS | `docs/m8-compatibility.md` truthfully defines the next-major support window, saved-config/previous-release rollback, Lima-native storage boundary, and four-row evidence matrix. |
| M8-010 exact cleanup and preservation | PASS | Final audit found no GraphX run ledgers, OVS bridges, namespaces, matching links, netem qdiscs, QEMU/dumpcap processes, lab containers, or Docker macvlan/ipvlan networks. Focused live tests preserve pre-existing Docker driver-network inventory. |

## Passing evidence

- Host quick: `outputs/verification/20260911T003807Z-quick.log` — 45/45 tests.
- Host quality: `outputs/verification/20260911T003844Z-quality.log` — formatting, clang-tidy, and cppcheck passed.
- Host sanitizers: `outputs/verification/20260911T003908Z-sanitizers.log` — 45/45 tests and configured sanitizer coverage passed.
- Host portable: `outputs/verification/20260911T003945Z-portable.log` — C++23 and C++20 suites, projections, migration, telemetry (77 tests), and web (17 tests/build) passed.
- Lima prerequisite verification passed with evidence at `/var/lib/graphx/m1/evidence/20260911T004112Z-0a73372f`.
- Separate Lima build: `/var/lib/graphx/m8-verification/build`.
- Privileged Lima regression: 6/6 M3-M8 live tests passed in 74.98 seconds.
- Two subsequent direct M8 runs each reported that all canonical OVS-only lab lifecycles passed twice.
- `git diff --check` passed, and M0 baseline/handoff files are unchanged from `HEAD`.

The Lima evidence applies only to macOS Apple Silicon plus Lima ARM64. Native
Linux ARM64, native Linux x86_64 TCG, and native Linux x86_64 KVM rows were not
executed and are not claimed.

No product files were modified during verification; this report is the only
verification-created file.
