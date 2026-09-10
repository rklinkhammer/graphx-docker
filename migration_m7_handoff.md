# Migration M7 implementation handoff

**Scope:** declarative OVS capture, timed faults, and layered diagnostics  
**Baseline:** independently verified M6 commit `a35cdd7`  
**Disposition:** implemented; awaiting independent M7 verification  
**Repository state:** intentionally uncommitted

## Result

M7 makes bounded Ethernet capture and timed netem faults part of the version-2
identity-owned infrastructure lifecycle. Capture remains separate from GraphX
LINKTYPE_USER0 application evidence. Telemetry and the web console now identify
policy, route, link, attachment, and application diagnostic layers.

| Requirement | Result | Principal evidence |
| --- | --- | --- |
| M7-001 strict declarations | Pass | `network.captures` and `network.faults` have bounded schema/parser fields, unique IDs, typed target checks, and cross-field validation. |
| M7-002 capture ownership | Pass | The ledger records graph/config/owner, mirror interface/ifindex, directory device/inode/UID/GID/mode, PID, and process start time. |
| M7-003 bounded PCAPNG | Pass | Dumpcap uses snap length plus file-size, duration, and file-count ring bounds in a private owner session. |
| M7-004 retention | Pass | Destroy removes every retained file write bit before sealing the session mode 0550; the next create prunes only expired, unchanged, root-owned read-only sessions containing approved regular files. |
| M7-005 safe export | Pass | `graphx infra capture export` uses the verified directory descriptor and requires a root-owned non-writable single-link source, complete final PCAPNG block, bounded size, and an exclusive no-follow destination. |
| M7-006 timed fault | Pass | Netem records exact interface/qdisc/timer plus boot-bound monotonic deadline identity, reports active/expired, and fails closed on premature disappearance. |
| M7-007 rollback/recovery | Pass | M7 failure and hard-crash checkpoints preserve enough state for exact rollback/recovery; capture evidence remains deliberately sealed. |
| M7-008 layered diagnostics | Pass | API/topology edges expose a derived diagnostic layer for policy, route, link, attachment, and application states; the inspector and styles preserve it. |
| M7-009 example/docs | Pass | `examples/network-observability` documents create/status/export/destroy, storage, retention, and separate trust domains. Architecture, infrastructure, upgrade, test, and phase prompts are updated. |
| M7-010 regression | Pass | Portable negative contracts and a privileged Linux two-cycle test cover rollback, crash recovery, ring bounds, retention, export, expiry, and cleanup. |

## Important implementation boundaries

- Capture directories must be beneath the root-owned
  `/var/lib/graphx/captures` boundary. Lima provisioning now enforces this;
  it is required by the Ubuntu dumpcap security behavior as well as M7's
  evidence boundary.
- Capture startup diagnostics are bounded and private. A failed start removes
  its incomplete session and reports the dumpcap cause.
- Retention cleanup never recursively removes an arbitrary tree. It accepts
  only `<capture-id>-<32 hex>` sealed directories with root identity and only
  single-link PCAPNG files plus `dumpcap.stderr`.
- Timers compare both the original ifindex and exact qdisc before automatic
  deletion. Interface reuse or qdisc replacement therefore causes a no-op and
  later lifecycle refusal rather than deletion of unrelated state.
- Status and cleanup accept a missing timer and qdisc as expired only after the
  recorded boot-bound monotonic deadline. Premature disappearance fails closed.
- Active capture status, export, destroy, and recovery require the recorded
  root-owned mode-0700 directory identity. Capture sources must also remain
  root-owned, single-link, and non-writable by group or other.
- After dumpcap stops, each approved file is reopened relative to the verified
  session descriptor, identity-checked, and stripped of all write bits before
  the directory is sealed. Retention pruning accepts only that exact shape.
- M7 implements timed netem. OVS port/drop-rule fault types remain a future
  extension, as stated in the migration plan.

## Verification completed during implementation

- `scripts/verify.sh quick`: pass, 44/44 tests.
- `scripts/verify.sh quality`: pass, formatting plus clang-tidy/cppcheck.
- `scripts/verify.sh sanitizers`: pass, 44 tests plus sanitizer coverage
  (LLVM 21 UBSan on macOS 26 per repository policy).
- `scripts/verify.sh portable`: pass for C++23 and supported C++20, including
  77 telemetry tests, 17 web tests, production web build, topology validation,
  and portable runtime exercises.
- Lima ARM64 Linux M7 regression: pass. The test itself ran two ordinary
  create/destroy cycles plus injected rollback and hard-crash recovery. It
  proved system OVS mirror traffic, a bounded two-file PCAPNG ring, complete
  export readable by `capinfos`, exclusive destination refusal, automatic
  netem expiry, early timer/qdisc-loss refusal, directory and capture-file mode
  drift refusal, expired-session pruning, and no bridge/link/namespace/qdisc/
  ledger/process residue.
- `git diff --check`: pass.

Implementation verification used the existing running Lima VM after applying
the new root-owned capture-boundary mode there. Because `provision.sh` changed,
the repository-derived Lima configuration digest has also changed. Independent
verification should recreate or reprovision the VM from the final candidate
before treating the Lima environment itself as authoritative.

## Independent verifier entry points

```sh
./scripts/verify.sh quick
./scripts/verify.sh quality
./scripts/verify.sh sanitizers
./scripts/verify.sh portable

# In a freshly provisioned GraphX Lima VM or native Linux, as root:
cmake -S . -B /var/lib/graphx/m1/build/dev -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DGRAPHX_BUILD_TESTS=ON \
  -DGRAPHX_ENABLE_LINUX_OVS_TESTS=ON
cmake --build /var/lib/graphx/m1/build/dev
python3 tests/test_m7_network_observability_live.py \
  /var/lib/graphx/m1/build/dev/graphx
```

The independent work package is `prompt/verifier.md`. Do not infer an M8
retirement decision from this handoff.
