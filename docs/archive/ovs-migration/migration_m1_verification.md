# Migration M1 verification and remediation

**Verified:** 2026-09-08 (America/New_York)  
**Repository:** `~/workspace/graphx-docker`  
**Branch/base:** `main` at `0b6150056a3dbea1f0a5404a006c4d45231f5ecb` (`M0 baseline`)  
**Product:** GraphX 1.1.0  
**Remediated:** 2026-09-09 (UTC)  
**Overall verdict:** **Pass**

## Executive result

The M1 Lima environment is real and reproducible. Starting from an absent
instance, the documented workflow created an ARM64 Ubuntu VM using Lima `vz`,
provisioned rootful Docker and system Open vSwitch, and completed two independent
provision/verify/cleanup transactions. Both transactions exchanged and captured
real ICMP packets through an OVS system-datapath bridge and a namespace veth.
They also created an owned TAP, exercised nftables and netem, passed the complete
37-test GraphX quick profile, verified projections, matched ordinary-path guest
before/after snapshots, and left no ordinary-path `gx-m1-*` resources.

The original independent pass found four failure-recovery and isolation
defects:

1. failures between creation and ownership tagging leak a namespace, veth pair,
   or TAP;
2. replacing an owned TAP with a foreign same-named object can leave that object
   behind while the verifier writes `status=passed`; and
3. failed runs bypass evidence rotation, allowing retained evidence to exceed
   the documented ten-directory limit.

A forged `/run/graphx-m1/owner` was also overwritten and removed without being
validated. Remediation replaced that cleanup model with exclusive transaction
state, exact kernel/OVS identities, signal-deferred create-and-record sections,
fatal cleanup propagation, complete snapshots, explicit post-cleanup assertions,
and success/failure evidence rotation. The complete adversarial suite and two
fresh final-digest lifecycle cycles now pass. LIMA-006, LIMA-007, LIMA-008, and
LIMA-010 are closed.

## Evidence classification

- **macOS host runtime:** platform, virtualization support, disk/memory,
  instance configuration, explicit and denied port forwards, lifecycle, and
  host interface/route observations.
- **Lima guest runtime:** services, mounts, storage, OVS/veth/TAP topology,
  packet delivery/capture, nftables, netem, QEMU accelerator probes, cleanup,
  retention, and adversarial failures.
- **portable runtime:** macOS and guest GraphX quick profiles and projection
  checks.
- **inspection only:** YAML/script/static-test/documentation review and
  confirmation that configuration v1 semantics remain unchanged.
- **historical failures, now closed:** partial-mutation cleanup, replacement
  identity handling, forged state handling, and failed-run evidence retention.
- **not applicable:** configuration v2, application-container veth attachment,
  QEMU TAP application profiles, and MPC8360E support are later phases.

## Platform and repository baseline

| Item | Independent observation |
|---|---|
| macOS | 26.6.2, build 25G83; Darwin 25.6.0, `arm64` |
| Hardware virtualization | `kern.hv_support=1` |
| Host memory | 64 GiB |
| Available host storage | 216 GiB at verification start |
| Lima | 2.2.0 |
| Repository | `main`, base `0b61500`; M1 implementation uncommitted |
| Existing instance | Initially `graphx`, stopped, matching digest; deliberately inspected, stopped, identity-resolved, deleted, and recreated from absent state |
| Final instance | `graphx`, stopped, `aarch64`, `vz`, correct checkout path and digest `d2109a9bc8cd39737964bd53091a0a9ec8af045ee674491801dc5794254118e9` |

The actual Lima configuration reported 4 CPUs, 8 GiB RAM, an 80 GiB disk, the
digest-pinned Ubuntu image, one writable source mount, disabled Lima
containerd, disabled Rosetta/nested virtualization, the explicit loopback
forward, and the match-any deny rule. No implementation files were changed
during independent verification.

## Portable regression and architecture boundary

`scripts/verify.sh quick` passed all 37 tests on macOS in 22.39 seconds. The M0
baseline had 36 tests; the only added entry is `graphx-lima-static`. The
projection check reported all four projections current.

The five M0 infrastructure-plan SHA-256 values were reproduced exactly:

| Configuration | Independent SHA-256 |
|---|---|
| `examples/macvlan/graphx.yaml` | `40e09c1cb7ed78b118efeb10dbfe42fc8b06eb10312091f4df7afd9f0920f69d` |
| `examples/ipvlan-l2/graphx.yaml` | `da18ff3dfd11a72a834dd6933e161db0a18b92a1ddfc352340208fd274f40f37` |
| `examples/ipvlan-l3/graphx.yaml` | `598abfa52b274b8d6a3362bc67b141f0852dc590a61b552d99c5960d3b06864b` |
| `examples/mixed-network/graphx.yaml` | `ede943d05d0e70cd4bb36970bf94be9ea81787568f313766a807a3c14b6b65fc` |
| `examples/static-route-policy/graphx.yaml` | `33c53b043727d2404f1dcf4a8186dd1ce0b92536b49ead19f20416a166a355db` |

Inspection confirmed the schema still accepts only version-1 Docker
`bridge`, `macvlan`, and `ipvlan` drivers; `src/infra.cpp` and the network model
retain their prior behavior. Current QEMU examples still declare slirp/user-mode
networking. M1 did not silently introduce version 2 or the later OVS-only
application data plane.

## Clean creation, provisioning, and boundary evidence

The existing stopped instance was inspected first. Both retained implementation
PCAPs contained two packets and their before/after snapshots matched. The exact
instance name, status, architecture, VM type, checkout parameter, and digest
were then resolved before `limactl delete graphx`. The source `VERSION` file
remained present after deletion, and a subsequent `start.sh` created a new VM
from the cached digest-pinned image.

The recreated guest reported:

| Surface | Runtime observation |
|---|---|
| Guest | Ubuntu 24.04.4 LTS, Linux 6.8.0-134-generic, `aarch64` |
| Source | `/workspace/graphx-docker`, writable `virtiofs` |
| Runtime state | `/var/lib/graphx`, Docker, OVS, build, QEMU and capture paths on `/dev/vda1` `ext4` |
| Docker | 29.1.3, rootful `docker.service`, `overlayfs`, `/var/lib/docker` |
| Compose | 2.40.3 |
| OVS | 3.3.9, enabled/active `openvswitch-switch.service`, guest-local root-owned sockets |
| QEMU | 8.2.2; x86_64 and PowerPC binaries listed TCG only |
| Build | CMake 3.28.3; guest build under `/var/lib/graphx/m1/build/dev` |
| Package manifest | 28 requested packages; SHA-256 `36160e4016ca6c3f62f920f4a73061942ebff7b47bd344c1ae778c8afd0e3f52` |
| Docker probe image | `hello-world@sha256:5e22040d441e5fb3aed38368acbe8486b575d7018df38dbdfbc7311fbb2ef3a9` |

An explicit second provisioning reported every requested package already at its
installed version, produced the identical package-manifest hash, and returned
both Docker and OVS to active state. This proves practical idempotence for the
tested repository state.

The configured `127.0.0.1:18080` forward successfully reached a temporary guest
listener on `127.0.0.1:8080`. A temporary listener on guest loopback port 38081
was not reachable on macOS port 38081. The instance directory exposed Lima SSH
and internal host-agent sockets only; no Docker, OVS, or QMP socket was
forwarded.

The ARM64 QEMU binary listed both KVM and TCG as compiled accelerators, but an
actual KVM launch failed because `/dev/kvm` was absent. The x86_64 and PowerPC
binaries listed TCG only. Documentation correctly avoids claiming KVM.

## Two independent runtime transactions

The independently recreated VM was provisioned before each run. Both calls to
`infrastructure/lima/verify.sh` passed and retained VM-local evidence:

| Cycle | Evidence | Packet result | Guest isolation result |
|---|---|---|---|
| 1 | `/var/lib/graphx/m1/evidence/20260909T013022Z-f54f21a3` | 2 sent, 2 received; 252-byte PCAP containing echo request and reply | All compared snapshots identical; no ordinary-path leftovers |
| 2 | `/var/lib/graphx/m1/evidence/20260909T013138Z-48bda17b` | 2 sent, 2 received; 252-byte PCAP containing echo request and reply | All compared snapshots identical; no ordinary-path leftovers |

Each runtime record showed `bridge_datapath=system`, UUID-bearing OVS
interfaces for `gx-m1-int`, `gx-m1-vh`, and `gx-m1-tap`, a persistent TAP owned
by the Lima user, namespace veth peer identity, the `10.254.83.0/30` route,
`inet gx_m1` rules, a 1 ms netem qdisc, and the bounded QEMU accelerator report.
TShark decoded packet 1 as ICMP echo request `10.254.83.2 -> 10.254.83.1` and
packet 2 as echo reply in the opposite direction.

The macOS interface/address set and Lima listener set did not change across the
transactions. The raw route-table comparison saw one new dynamic LAN neighbor
cache entry and changing neighbor expiry timers; inspection found no new
configured route or GraphX/Lima route mutation. The VM's `vz` NAT and SSH
boundary were the only host execution changes.

## Adversarial results

### Passed behavior

- An occupied `gx-m1-tap` with alias `graphx-m1:independent-foreign` caused a
  preflight failure and remained unchanged.
- A PATH-controlled missing `qemu-system-aarch64` failed before mutation with
  an actionable diagnostic.
- Replacing the source mount with an ext4 bind and presenting the shared
  virtiofs filesystem as `/var/lib/graphx` both failed before mutation.
- Stopped Docker and stopped OVS each caused an actionable pre-mutation failure;
  service restart allowed immediate retry.
- A PATH-controlled tcpdump denial failed after topology creation and removed
  all normally tagged resources.
- The supported post-topology failure hook cleaned its ordinarily tagged
  resources, and a later successful verification passed immediately.
- Repeated stop was a successful no-op. Restart preserved source and retained
  VM-local evidence. Final state is stopped.
- After the retention failure test, one successful run restored the evidence
  directory count to ten.

### Original failed behavior (closed by remediation)

#### 1. Creation-to-tag interruption windows leak resources

The verifier creates several resources in one command and tags them in the next:

- namespace creation at `verify.sh:172`, owner alias at line 173;
- veth creation at line 174, host alias at line 175; and
- TAP creation at line 185, owner alias at line 186.

PATH-controlled `ip` failures at each second command reproduced these results:

- the untagged `gx-m1-ns` namespace survived cleanup;
- the untagged `gx-m1-vh@gx-m1-vn` pair survived cleanup; and
- the untagged `gx-m1-tap` TAP survived cleanup.

The test objects were independently inspected and then removed by exact name.
No unrelated object was deleted. The single post-topology test hook cannot cover
these earlier mutation boundaries.

**Implemented closure:** exact kernel/OVS identity is recorded immediately after
creation in protected transaction state. Signal-deferred create-and-record
sections close the unowned interruption windows, and deterministic hooks cover
every mutation boundary.

#### 2. Foreign replacement can produce a false pass

While the verifier was running, its token-owned `gx-m1-tap` was deleted and
replaced by a dummy link named `gx-m1-tap` with alias
`graphx-m1:independent-replacement`. The verifier preserved the foreign object,
which is correct, but returned success and wrote a `status=passed` result for
`20260909T013447Z-7f78b8ed` while the replacement remained present.

Three implementation details combine to cause the false result:

- `cleanup()` executes `set +e` at line 59 and does not restore fail-fast mode;
- the explicit `cleanup` return at line 228 therefore does not terminate the
  successful path when ownership checking fails; and
- snapshots remove every link whose name starts with `gx-m1-` at line 115, so
  the leaked replacement is invisible to the before/after comparison.

There is no explicit assertion after cleanup that every expected owned resource
is absent or that a preserved replacement forces the final result to fail.

**Implemented closure:** cleanup no longer changes shell error mode; its result
is explicitly fatal. Expected and observed replacement identities are written
to `cleanup-errors.txt`, complete snapshots no longer hide `gx-m1-*` names, and
exact post-cleanup assertions gate `status=passed`.

#### 3. Forged run state is overwritten

An existing `/run/graphx-m1` containing owner `foreign-owner` and a sentinel was
not rejected. Lines 158-159 reused the directory and overwrote its owner before
the test topology was created. Cleanup then deleted the forged owner file. The
sentinel survived and caused `rmdir` to fail, but the previous ownership record
had already been destroyed.

**Implemented closure:** state uses atomic exclusive directory creation and a
random owner. Any pre-existing file, symlink, or directory is rejected before
mutation and is not modified or adopted.

#### 4. Failed evidence retention is unbounded

Before the test there were 8 evidence directories. Twelve bounded
post-topology failures increased the count to 20. Rotation is performed only at
lines 246-253 after the success result has already been written; failure traps
never invoke it. Repeated failures can therefore grow the evidence tree without
the promised bound.

**Implemented closure:** path-validated rotation runs before allocation and from
both success and failure exits. It preserves the current diagnostic directory
while enforcing a ten-directory and 256 MiB aggregate bound.

## Remediation verification

The remediated implementation was first exercised against the retained guest,
then the old VM was stopped, identity-resolved, deleted, and recreated from the
final digest. The source checkout remained present. The fresh instance completed
two host-driven `start/verify/cleanup` cycles with a stop/start boundary:

| Cycle | Evidence at completion | Result |
|---|---|---|
| Fresh cycle 1 | `/var/lib/graphx/m1/evidence/20260909T024205Z-0e61acfe` | Passed full guest quick profile, projections, packet capture, exact cleanup, and complete snapshot comparison |
| Fresh cycle 2 | `/var/lib/graphx/m1/evidence/20260909T024330Z-c81e6009` | Passed after stop/start with the same checks; retained at handoff |

The following adversarial checks were run against the same guest platform with
the final verifier mounted from the source checkout before the definitive VM
recreation:

- all 15 deterministic failure stages (`state`, Docker, bridge, internal port,
  namespace create/own, veth create/own/attach, TAP create/own/attach, netem,
  topology, and capture start) failed as requested and left no container,
  link, namespace, bridge, or transaction state;
- after every failed run, evidence remained at or below ten directories and
  256 MiB (observed maximum after the final sweep: 10 directories and 680 KiB);
- a pre-existing state directory containing a foreign owner and sentinel was
  rejected, with both files byte-for-byte unchanged;
- replacing the OVS bridge, internal port, namespace, host veth, namespace veth,
  or TAP with a same-named foreign object caused a nonzero result, preserved the
  replacement (and its containing namespace when required), recorded expected
  and observed UUID/inode/ifindex values, wrote no `result.txt`, and cleaned all
  other transaction resources;
- after deliberate removal of the test dummy, an immediate normal retry passed.

The final retained evidence count is two directories using 252 KiB. Its latest
`result.txt` records ARM64 Linux 6.8.0-134, virtiofs source, ext4 state, Docker
29.1.3, and OVS 3.3.9. The instance is stopped, and a repeated stop was a
successful no-op. The final macOS portable run also passed 37/37 tests in 19.99
seconds (25-second quick profile), and all projections were current.

## Acceptance matrix

| ID | Requirement | Independent evidence | Status | Remediation |
|---|---|---|---|---|
| LIMA-001 | Deterministic, bounded definition and local exposure | Actual Lima config matched pinned ARM64/vz image, 4 CPU/8 GiB/80 GiB bounds, single source mount, explicit loopback forward, deny fallback; live forward tests passed | Implemented | None for M1 |
| LIMA-002 | Idempotent provisioning and healthy rootful Docker/OVS | Clean boot plus two explicit provisions; identical 28-package manifest; enabled/active system services | Implemented | Consider a package snapshot if byte-identical future rebuilds become required |
| LIMA-003 | High-I/O state is VM-local | Source was virtiofs; Docker, OVS, GraphX build/QEMU/capture/evidence state was ext4 | Implemented | None |
| LIMA-004 | Real OVS, namespace, veth, TAP, routing, nftables, netem, capture | Two real packet transactions and independent PCAP/state inspection | Implemented | None |
| LIMA-005 | Current GraphX baseline | 37/37 on macOS and both guest cycles; projections current; M0 plan hashes unchanged | Implemented | None |
| LIMA-006 | Bounded, deterministic start/verify/stop/interruption/retry/cleanup | Two fresh final-digest cycles passed; all 15 mutation-boundary failures cleaned exactly; failed-run evidence remained within count/byte bounds | Implemented | None |
| LIMA-007 | Unrelated host/guest state preserved and owned resources removed | Complete snapshots matched on successful runs; a same-named foreign replacement was preserved and forced a nonzero result with identity evidence | Implemented | None |
| LIMA-008 | Privileged boundary and identity-safe mutation | Exclusive state, exact OVS UUID/namespace inode/link ifindex records, and fatal cleanup propagation passed forged-state and replacement tests | Implemented | None |
| LIMA-009 | No premature v2, app-veth, QEMU-TAP, or KVM claim | Source, schemas, plan hashes, examples, runtime KVM probe, and documentation agree | Implemented | None |
| LIMA-010 | Reproducible and accurate operator documentation | Operator guide now documents exclusive state, exact identities, preserved foreign replacements, complete snapshots, and success/failure retention bounds | Implemented | None |

## Verdict and release gate

M1 **passes**. All ten LIMA requirements are implemented. The four original
adversarial defects were reproduced before remediation, closed in the verifier,
and retested against a VM created from the final configuration digest. The
portable 37-test macOS gate, two clean guest transactions, all mutation-boundary
failures, forged-state preservation, same-name replacement handling, immediate
retry, retention bounds, and repeated stop behavior passed.

The final `graphx` instance is stopped with digest
`d2109a9bc8cd39737964bd53091a0a9ec8af045ee674491801dc5794254118e9`.
Two bounded successful evidence directories remain on its VM-local disk.
