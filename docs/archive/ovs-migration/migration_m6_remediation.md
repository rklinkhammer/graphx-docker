# Migration M6 remediation handoff

**Recorded:** 2026-09-10 (America/New_York)  
**Repository:** `~/workspace/graphx-docker`  
**Source report:** `migration_m6_verification.md`  
**Implementation state:** uncommitted and ready for independent re-verification

## Outcome

Both M6 verification blockers are remediated. The checked-in Lima environment
now provisions the Buildx dependency required by GraphX's BuildKit Dockerfiles,
and the stale VM was replaced by a clean ARM64/VZ instance whose recorded
configuration digest matches the candidate. During clean testing, an additional
login-user Docker access defect was found and fixed in the provisioning and
lifecycle contracts.

This is an implementation handoff. It does not replace the independent verdict
in `migration_m6_verification.md`; a separate verifier must re-evaluate the
working tree before M6 is accepted or M7 begins.

## Finding closure

| Finding | Remediation | Evidence | State |
|---|---|---|---|
| M6-V1: stale Lima environment | Deliberately removed the exact `graphx` ARM64/VZ instance mounted to this repository and recreated it from the final candidate. | The final instance has the candidate digest and `infrastructure/lima/verify.sh` passed with retained evidence at `/var/lib/graphx/m1/evidence/20260910T164601Z-282c0c2d`. | Remediated |
| M6-V2: Buildx missing | Added `docker-buildx` to provisioning, readiness and evidence probes, the authoritative verifier, portable tests, documentation, and the QEMU builder's prerequisite check. | Fresh VM reports Buildx 0.30.1. The root project image and QEMU builder image both completed BuildKit secret-mount stages. | Remediated |
| Discovered: login user could not access rootful Docker after first provisioning | Provisioning adds and tests the Lima user in the Docker group. `start.sh` performs one bounded refresh restart when the initial SSH control session retains pre-provisioning groups, then fails closed unless Docker and Buildx work without `sudo`. | The final clean start took the refresh path; the login user then reported group `docker`, `/var/lib/docker`, Buildx 0.30.1, and completed both checked-in builds without `sudo`. | Remediated |

## Implementation surface

- `infrastructure/lima/provision.sh`: installs/records Buildx, adds the login
  user to the Docker group, and validates user-level daemon access.
- `infrastructure/lima/graphx.yaml`: readiness requires Buildx and Docker-group
  membership.
- `infrastructure/lima/start.sh`: bounded initial-session refresh and final
  non-sudo Docker/Buildx probe.
- `infrastructure/lima/verify.sh`: independently checks the login identity's
  rootful Docker access and records Buildx evidence.
- `infrastructure/lima/README.md`: Buildx and Docker-group security/lifecycle
  behavior.
- `examples/qemu-node/scripts/build.sh`: explicit Buildx prerequisite.
- `examples/qemu-node/scripts/test.sh` and `tests/test_lima_environment.py`:
  portable regression contracts for the new behavior.

## Validation evidence

- Final macOS quick profile: 42/42 tests passed.
- Quality profile: LLVM 21 format and clang-tidy/cppcheck passed.
- Focused Lima static and QEMU portable suites passed.
- Final clean ARM64/VZ creation passed all bounded readiness checks, including
  the automatic supplementary-group refresh.
- Authoritative Lima verification passed system OVS, TAP/veth/namespaces,
  nftables, netem, packet capture, Docker, GraphX quick tests, and exact cleanup.
- `docker build -t graphx-demo:latest .` completed as the ordinary Lima user.
- `examples/qemu-node/scripts/build.sh` completed as the ordinary Lima user,
  producing Buildroot 2025.02.17 guest artifacts:
  - `bzImage`: 6,329,344 bytes,
    SHA-256 `61bdc74cd32ff9bd801ec42cd07d45abbaea9fdd303ca8e9e9fff74697dc99de`;
  - `rootfs.cpio.gz`: 1,003,664 bytes,
    SHA-256 `7baa4fb05d012b43330fa0c48285ba0b125133a8155e8ec07fe453d0bb354fa3`.
- M4 live passed using the normal fresh `graphx-demo:latest` image.
- Two M6 live-regression invocations passed, each including two lifecycle
  cycles plus rollback, crash recovery, replacement refusal, and cleanup.
- The actual guest TAP profile passed TCG/QMP, TCP and UDP, learned guest MAC,
  VLAN 42 communication and VLAN 43 isolation, broadcast/multicast, SPAN,
  pause/resume, netem, and teardown.
- Final runtime audit found no M6 bridge, TAP, veth, namespace, netem qdisc,
  ownership ledger, or QEMU process. `git diff --check` passed.

## Re-verification focus

Start from the final clean `graphx` VM and first confirm its candidate digest,
Buildx package/version, login-user Docker access, and authoritative Lima
verification. Rebuild at least one BuildKit image without `sudo`, repeat the M6
privileged lifecycle twice and the actual guest profile, then audit exact
cleanup. Retain the original independent report unchanged and issue a separate
M6 re-verification verdict.
