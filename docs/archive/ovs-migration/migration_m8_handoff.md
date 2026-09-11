# Migration M8 implementation handoff

**Scope:** compatibility and release closure  
**Baseline:** independently verified M7 commit `e747adbea15a8d0b399f72ea13d631e71faf6d2b`  
**Disposition:** implemented and remediated; awaiting independent M8 re-verification  
**Repository state:** intentionally uncommitted

## Result

M8 leaves one active GraphX-managed network realization: configuration version
2 through the persistent system-OVS ownership lifecycle. Containers use veth,
QEMU uses TAP, and MACVLAN/IPVLAN names are semantic profiles. Compose supplies
process lifetime and management connectivity only.

| Requirement | Result | Principal evidence |
| --- | --- | --- |
| M8-001 v1 compatibility | Pass | Seven curated fixtures validate, inspect, project, and migrate deterministically; create/destroy/status/recover/route/capture reject before state mutation. |
| M8-002 sole production backend | Pass | The Docker-network planner, `InfraAction`, and imperative `netem_command` API are removed; v2 dispatches only to the owned OVS lifecycle. |
| M8-003 canonical labs | Pass | Mixed, MACVLAN, IPvlan-L2/L3, route-policy, and SDR configs are canonical v2 `graphx.yaml`; active Compose is management-only. |
| M8-004 legacy retirement | Pass | Docker-driver manifests, split launchers, Docker Desktop simulation, and `docker/ovs` image are removed. |
| M8-005 QEMU default | Pass | `examples/qemu-node/scripts/demo.sh` selects TAP/OVS; external/container usernet profiles are explicit deprecated compatibility paths. |
| M8-006 migration docs | Pass | ADR 0018, `docs/m8-compatibility.md`, upgrade, architecture, infrastructure, example, and test guides describe the support and rollback boundary. |
| M8-007 portable regression | Pass | `test_m8_compatibility_closure.py` scans production/examples and exercises v1 refusal plus canonical plans/defaults. |
| M8-008 live regression | Pass | `test_m8_compatibility_closure_live.py` runs four canonical Compose labs through two OVS lifecycle cycles each and compares Docker driver networks. |
| M8-009 prior contracts | Pass | M3-M7 portable and privileged tests remain green after their fixtures were moved to compatibility scope. |
| M8-010 cleanup/platform truth | Pass on available platform | Lima audit found no matching containers, Docker data networks, OVS bridges, namespaces, links, qdiscs, ledgers, capture/QEMU processes, or timers. Unavailable native/KVM rows are not claimed. |

## Compatibility boundary

- Version 1 remains readable by `validate`, `inspect`, `project`, and
  `config migrate` through the next major release.
- Every supported infrastructure operation loads the configuration and rejects
  version 1 before platform mutation. The retired fault entry point always
  directs operators to declarative `network.faults` and v1 migration.
- Curated Docker-driver inputs moved to `examples/compatibility/v1`. Old
  behavior is reproducible only from version-control history, not an active
  privileged launcher.
- The standard unprivileged application demo may still use ordinary Compose
  service networking; it is not a GraphX-managed network-infrastructure backend.
- QEMU external/container user networking is retained only for its separately
  documented deprecation window. No root/default launcher selects it.

## Verification completed during implementation

- `scripts/verify.sh quick`: pass, 45/45 tests.
- `scripts/verify.sh quality`: pass, 49-file formatting plus clang-tidy/cppcheck.
- `scripts/verify.sh sanitizers`: pass, 45 tests plus sanitizer coverage using
  LLVM 21 UBSan on macOS 26 as required by repository policy; the packaging-only
  test is intentionally disabled in sanitizer builds.
- `scripts/verify.sh portable`: pass for C++23 and C++20, including 77 telemetry
  tests, 17 web tests, production web build, topology/migration/projection
  checks, and finite process/transport exercises.
- `infrastructure/lima/verify.sh`: pass on the configured Apple Silicon ARM64
  Lima VM; evidence `/var/lib/graphx/m1/evidence/20260911T001733Z-fdaf05e2`.
- Lima privileged CTest selection `graphx-m[3-8].*-live`: pass, 6/6.
- Expanded M8 live closure: pass twice independently. Each invocation ran
  `macvlan`, `ipvlan-l2`, `ipvlan-l3`, and `mixed-network` through two complete
  create/status/destroy cycles with management-only Compose and unchanged
  Docker macvlan/ipvlan network inventory.
- Exact final Lima residue audit: pass. Retained M7 capture sessions were not
  treated as live residue; the final audit found none present in this VM.
- `git diff --check`, changed shell syntax, and M8 Python compilation: pass.

Implementation used the existing configured `graphx` Lima VM, then reran its
authoritative prerequisite verifier before privileged testing. M8 remediation
pins Node.js 24 in the Lima definition and provisioning contract. Independent
verification should recreate the now-stale pre-remediation VM for fresh release
acceptance using the documented deliberate-removal procedure.

## Platform evidence status

| Platform | Implementation evidence | Release claim |
| --- | --- | --- |
| macOS Apple Silicon host | quick, quality, UBSan, portable | Pass for host-portable behavior |
| Lima ARM64 Linux | prerequisite verifier, M3-M8 privileged regressions, TCG-oriented QEMU TAP contract | Pass for Lima ARM64 |
| native Linux ARM64 | Not run in this implementation environment | Unavailable; no claim |
| native Linux x86_64 + TCG | Not run | Unavailable; no claim |
| native Linux x86_64 + KVM | Not run | Unavailable; no claim |

## Independent verifier entry points

```sh
scripts/verify.sh quick
scripts/verify.sh quality
scripts/verify.sh sanitizers
scripts/verify.sh portable
infrastructure/lima/verify.sh

# Inside Lima or native Linux as root:
cmake -S . -B /var/lib/graphx/m8/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DGRAPHX_BUILD_TESTS=ON \
  -DGRAPHX_ENABLE_LINUX_OVS_TESTS=ON
cmake --build /var/lib/graphx/m8/build
ctest --test-dir /var/lib/graphx/m8/build --output-on-failure \
  -R 'graphx-m[3-8].*-live'
python3 tests/test_m8_compatibility_closure_live.py \
  /var/lib/graphx/m8/build/graphx /workspace/graphx-docker
```

The independent work package is `prompt/verifier.md`. Do not commit, publish,
or infer unavailable native-Linux/KVM evidence from this handoff.
