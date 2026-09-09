# Migration M1 implementation handoff

**Recorded:** 2026-09-08 (America/New_York)  
**Repository:** `~/workspace/graphx-docker`  
**Branch/base:** `main` at `0b6150056a3dbea1f0a5404a006c4d45231f5ecb` (`M0 baseline`)  
**Product:** GraphX 1.1.0  
**Implementation state:** uncommitted for independent review

## Verdict

Migration M1 is implemented and its independent findings are remediated. The
final VM was created from configuration digest
`d2109a9bc8cd39737964bd53091a0a9ec8af045ee674491801dc5794254118e9`
and passed two ARM64 Linux system-OVS/veth/TAP cycles separated by stop/start.
Both passed rootful Docker, packet capture, the 37-test GraphX quick profile,
projections, complete before/after comparison, and exact cleanup. The instance
is stopped with both successful evidence directories retained.

M1 changes the execution environment only. Configuration version 1, its current
Docker bridge/MACVLAN/IPVLAN realization, and QEMU slirp remain implemented.
Configuration version 2, OVS-only application realization, container veth
attachment, and QEMU TAP are not claimed.

## Acceptance matrix

| ID | Requirement | Implementation and evidence | Status |
|---|---|---|---|
| LIMA-001 | Deterministic bounded definition | `graphx.yaml` pins ARM64 Ubuntu image digest, `vz`, 4 CPUs, 8 GiB RAM, 80 GiB disk, one virtiofs source mount, and one explicit loopback forward followed by match-any deny. Lifecycle commands have hard deadlines. | Implemented |
| LIMA-002 | Idempotent provisioning | `provision.sh` uses three bounded apt attempts, records the complete package manifest, configures rootful Docker and system OVS, and waits for both systemd services. A second run reported zero new/upgraded packages and both services active. | Implemented |
| LIMA-003 | VM-local privileged state | Verification observed source `virtiofs` and state `ext4`; Docker, OVS, QEMU, capture, build, run, log, and evidence paths resolve to the VM-local filesystem. | Implemented |
| LIMA-004 | Real Linux primitives | Both clean cycles created a system-datapath OVS bridge, namespace/veth pair, owned TAP, IPv4 path, nftables rule, netem qdisc, ICMP exchange, and tcpdump/TShark packet evidence. | Implemented |
| LIMA-005 | GraphX baseline | macOS and both clean guest cycles passed all 37 CTest entries. Guest builds used `/var/lib/graphx/m1/build/dev`; all projection checks were current. | Implemented |
| LIMA-006 | Bounded lifecycle | Clean create/start/verify/cleanup/stop ran twice. All 15 mutation-boundary failure hooks cleaned exactly; success and failure evidence remained bounded. | Implemented |
| LIMA-007 | Isolation | Complete successful snapshots matched. Foreign same-name bridge, internal, namespace, veth, and TAP replacements were preserved and made verification fail. | Implemented |
| LIMA-008 | Security | No privileged socket is forwarded. Exclusive state and exact OVS UUID, namespace inode, internal/veth/TAP ifindex, label, and namespace-inventory checks gate cleanup. | Implemented |
| LIMA-009 | Architecture honesty | README, support, security, and architecture documents distinguish M1 tooling from later OVS-only/veth/QEMU-TAP work and report TCG without claiming KVM. | Implemented |
| LIMA-010 | Operator documentation | `infrastructure/lima/README.md` covers prerequisites, storage/security boundaries, lifecycle, inspection, troubleshooting, limitations, and exact deliberate removal. | Implemented |

## Changed paths

- `infrastructure/lima/graphx.yaml` - authoritative Lima definition.
- `infrastructure/lima/provision.sh` - bounded idempotent guest provisioning.
- `infrastructure/lima/start.sh`, `stop.sh`, `common.sh`, and
  `run-bounded.py` - identity-safe host lifecycle and deadlines.
- `infrastructure/lima/verify.sh` - guest/runtime acceptance transaction,
  evidence, cleanup, isolation comparison, and deterministic failure hook.
- `infrastructure/lima/README.md` - operator guide.
- `tests/test_lima_environment.py` and `CMakeLists.txt` - portable M1 static
  contract registered as `graphx-lima-static`.
- `scripts/verify.sh` - optional `GRAPHX_DEV_BUILD_DIR` support so Linux and
  macOS CMake caches can remain on different filesystems; default behavior is
  unchanged.
- `README.md`, `SUPPORT.md`, `docs/security.md`, and
  `docs/GraphX_Architecture.md` - M1 boundary and navigation.
- `docs/GraphX_Architecture.docx` - regenerated editable architecture edition;
  all 23 rendered pages were visually inspected.
- `migration_m1_handoff.md` - this record.

No schema, loader, network planner, Compose file, network example, or QEMU
profile was changed.

## Runtime evidence

### Host

- macOS 26.6.2 (25G83), Apple Silicon `arm64`.
- Lima 2.2.0 using Virtualization.framework (`vz`).
- Final instance record before handoff:
  `graphx|Stopped|aarch64|vz|/Users/rklinkhammer/workspace/graphx-docker|d2109a9bc8cd...`.
- Final macOS quick run: 37/37 CTest entries passed in 19.99 seconds; profile
  completed in 25 seconds.
- Host log:
  `outputs/verification/20260909T023056Z-quick.log`.
- `graphx project graphx.yaml --check --output-dir config` reported all
  projections current.
- `limactl validate infrastructure/lima/graphx.yaml`, all shell syntax checks,
  `graphx-lima-static`, documentation consistency, and `git diff --check`
  passed.

### Guest

- Ubuntu 24.04.4 LTS, `aarch64`, Linux 6.8.0-134-generic.
- Docker Engine 29.1.3, rootful system service, data root `/var/lib/docker`.
- Open vSwitch 3.3.9, system service and disposable `datapath_type=system`.
- QEMU 8.2.2; the ARM64 binary reported `kvm` and `tcg`, but M1 neither
  requires nor claims usable KVM. Cross-architecture guests remain TCG.
- CMake 3.28.3.
- Docker probe image resolved to
  `hello-world@sha256:5e22040d441e5fb3aed38368acbe8486b575d7018df38dbdfbc7311fbb2ef3a9`.
- Fresh final-digest cycle 1 retained evidence:
  `/var/lib/graphx/m1/evidence/20260909T024205Z-0e61acfe`.
- Stop/start cycle 2 retained evidence:
  `/var/lib/graphx/m1/evidence/20260909T024330Z-c81e6009`.

Each retained directory contains before/after state, Docker output, ping,
packet PCAP, decoded TShark output, OVS/link/namespace/nftables/netem/QEMU
evidence, GraphX quick output, projection output, and `result.txt`. Evidence is
VM-local and the verifier retains at most ten timestamp/token directories.

## Adversarial results

- An occupied `gx-m1-tap` dummy link with a foreign alias caused a preflight
  failure and remained untouched.
- All 15 `GRAPHX_M1_TEST_FAIL_AFTER` stages from state allocation through
  capture start returned failure and removed every exact transaction resource.
- Pre-existing foreign run state was rejected without changing either its owner
  or sentinel file.
- Same-named foreign replacements for the OVS bridge, internal link, namespace,
  host veth, namespace veth, and TAP were preserved. Each mismatch forced a
  nonzero verifier result and recorded expected/observed identities; no false
  `status=passed` result was written.
- More than ten consecutive failure runs retained no more than ten evidence
  directories and remained well below the 256 MiB aggregate bound.
- Stopped Docker service/socket caused an actionable failure before mutation;
  restarting the services allowed immediate continuation.
- Repeated stop returned success without mutation.
- Restart of the stopped second VM retained both mounted source and VM-local
  evidence, and another complete verification passed.
- During development, strict snapshot comparison detected changing unrelated
  nftables counters. The final comparison preserves the full ruleset but
  normalizes only packet/byte counters, which are expected to advance during
  network tests.

The original independent findings and their remediation evidence are recorded
in `migration_m1_verification.md`. Deterministic creation-boundary injection and
same-name replacement tests are now part of that record; arbitrary host power
loss cannot be made transactional by a guest shell and remains an operator
recovery scenario guarded by exclusive stale-state refusal.

## Limitations and risks

- The Ubuntu disk image is digest pinned, while packages come from the image's
  Noble repositories. Exact installed versions are recorded in
  `/var/lib/graphx/m1/package-versions.tsv`; repository availability and future
  security revisions may change a newly provisioned manifest.
- The only application port forwarding is `127.0.0.1:18080` to guest
  `127.0.0.1:8080`. It is inactive unless a guest process listens. Lima's own
  dynamically selected SSH transport remains necessary.
- The VM uses 80 GiB maximum sparse disk allocation and retains at most ten
  evidence runs. Deliberate `limactl delete graphx` destroys all VM-local state.
- QEMU system packages are present, but M1 adds no QEMU TAP application profile
  and no MPC8360E machine implementation.
- This is an implementation handoff, not the independent report required by
  `prompt/verifier.md`.

## Independent verification

Start from the stopped retained instance for inspection, then follow
`prompt/verifier.md`. At minimum:

```bash
infrastructure/lima/start.sh
infrastructure/lima/verify.sh
infrastructure/lima/stop.sh
python3 tests/test_lima_environment.py .
scripts/verify.sh quick
./build/dev/graphx project graphx.yaml --check --output-dir config
```

Before testing deliberate removal, resolve the exact `graphx` instance name,
status, architecture, virtualization type, repository parameter, and digest as
documented in `infrastructure/lima/README.md`. Do not treat this handoff or its
paths as substitutes for inspecting the retained runtime evidence.
