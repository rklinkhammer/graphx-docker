# Migration M8 independent re-verification

Date: 2026-09-10 (America/New_York)  
Candidate: working tree based on `e747adbea15a8d0b399f72ea13d631e71faf6d2b`  
Verdict: **PASS**

The M8 remediation closes all three blocking findings from
`migration_m8_verification.md`. The release candidate now completes the
documented repository and privileged Linux acceptance paths, keeps version 1
migration-only, and leaves no live GraphX network infrastructure after the
canonical laboratory cycles.

## Finding closure

| Prior finding | Result | Independent evidence |
| --- | --- | --- |
| M8-V01: Linux acceptance selected deleted launchers | **CLOSED** | `scripts/test-features.sh linux-network` completed in a fresh Lima VM and ran all four labs through their executable canonical `up.sh`, `status.sh`, and `down.sh` wrappers. Static closure also rejects every retired launcher name and requires the Linux-built `GRAPHX_BIN`. |
| M8-V02: active documentation named deleted launchers | **CLOSED** | The active-source scan found no retired launcher/manifests outside excluded historical ADR/runbook material; documentation consistency passed. |
| M8-V03: Lima provisioned unsupported Node.js 18 | **CLOSED** | Fresh provisioning installed Node.js `v24.20.0` and npm `11.19.0`; `node:sqlite` loaded successfully. The complete telemetry suite passed in the documented Linux gate. |

## Requirement results

| Requirement | Result | Independent evidence |
| --- | --- | --- |
| M8-001 fresh build and repository gates | PASS | Host quick, quality, sanitizer, and clean portable gates passed. A fresh ARM64 Lima build completed, and the documented `linux-network` gate passed. |
| M8-002 v1 read/project/migrate; infra refusal before mutation | PASS | The portable M8 closure exercised all seven curated v1 fixtures: validate, inspect, project, deterministic migration, and pre-mutation refusal of create/destroy/status/recover/route/capture operations. |
| M8-003 no Docker-network realization or legacy imperative planner | PASS | Production scans found no Docker network realization, `netem_command`, or retired `InfraAction`; only the generic identity-owned `execute_infrastructure_plan` executor remains. |
| M8-004 canonical labs are v2, OVS-only, management-only Compose | PASS | Static checks and live runs confirmed the canonical configurations plan OVS system bridges and use veth attachments. Canonical Compose files contain no macvlan/ipvlan driver or external data network. |
| M8-005 v1 inputs fixture-only; no active legacy launchers/docs | PASS | The only macvlan/ipvlan driver declaration is the intentional fixture `examples/compatibility/v1/ipvlan-l3.yaml`. Active scripts and documentation contain none of the retired entry-point vocabulary. |
| M8-006 Docker Desktop/userspace-OVS simulation absent | PASS | Portable closure confirmed the retired `docker/ovs` implementation and parallel simulator launchers/manifests are absent. |
| M8-007 QEMU defaults to TAP/OVS | PASS | Portable closure validated the default TAP/OVS launcher and plan and confirmed that retained external/container user-network profiles are explicitly deprecated compatibility paths. |
| M8-008 Lima M3-M7 plus M8 twice | PASS | In a newly provisioned Apple Silicon Lima VM, the focused M3-M8 live suite passed 6/6 in 76.21 seconds. Two subsequent independent M8 invocations also passed; each invocation performed two complete cycles for all four canonical labs. |
| M8-009 compatibility, rollback, storage, platform matrix | PASS | Documentation consistency and M8 closure checks passed for the support window, migration/rollback procedure, VM-local runtime storage, and honest platform matrix. |
| M8-010 exact cleanup and preservation | PASS | Final guest audit found no GraphX OVS bridges, namespaces, veth/TAP devices, netem qdiscs, containers, Docker macvlan/ipvlan networks, YAML ledgers, capture files, timers, QEMU, or dumpcap processes. Live regressions also verify unrelated Docker network preservation. Four inactive per-graph lock files remain by design; each is a zero-byte regular file owned by root with mode `0600`, and none was held open. |

## Re-verification evidence

- Host quick: `outputs/verification/20260911T013306Z-quick.log` — 45/45 tests.
- Host quality: `outputs/verification/20260911T013350Z-quality.log` — formatting, clang-tidy, and cppcheck passed.
- Host sanitizers: `outputs/verification/20260911T013412Z-sanitizers.log` — 45 enabled tests plus sanitizer coverage passed; packaging remained disabled by policy.
- Host portable: `outputs/verification/20260911T014014Z-portable.log` — C++23 45/45, C++20 45/45, topology/projection/migration gates, telemetry 77/77, and web 17/17 plus production build.
- Focused portable M8 closure, Lima static contract, documentation consistency, shell wrapper modes, `git diff --check`, and M0 baseline/handoff immutability checks passed.
- Fresh Lima configuration digest: `8b72002058e8228b65f6eae315d565f565e78f5cb8954ce1f840294eb0746480`.
- Fresh Lima prerequisite verifier: PASS at `/var/lib/graphx/m1/evidence/20260911T014142Z-76f0f3d2`; evidence recorded ARM64, Linux 6.8, virtiofs source, ext4 state, Docker 29.1.3, and system OVS 3.3.9.
- Fresh guest Debug build: `/var/lib/graphx/m8-reverification/build`.
- Documented guest `linux-network` gate: PASS, including both 45-test C++ modes, telemetry/web, bounded UDP behavior, and the four canonical OVS laboratory lifecycles.

One earlier host portable attempt was explicitly terminated and excluded after
a transient shared-memory process-start race; its process exit was 143 despite
the wrapper's final PASS text. The clean rerun above exited zero and is the
evidence used for this verdict. This did not recur in the fresh Lima gate.

The platform evidence applies to macOS Apple Silicon plus Lima ARM64. Native
Linux ARM64, native Linux x86_64 TCG, and native Linux x86_64 KVM were not
executed and are not claimed.

The verifier-only `graphx-m8-verifier` VM was stopped and deleted after the
audit. The pre-existing `graphx` VM was restored to its original Running state.
No product files were changed during re-verification; this report is the only
verification-created file.
