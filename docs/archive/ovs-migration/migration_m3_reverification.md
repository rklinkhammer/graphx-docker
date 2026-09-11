# Migration M3 independent re-verification

**Verified:** 2026-09-09 (America/New_York)  
**Repository:** `~/workspace/graphx-docker`  
**Branch/base:** `main` at `b16c9631ffac7151d9b72ae634b8838f7515b606`  
**Product:** GraphX 1.1.0  
**Prior report:** `migration_m3_verification.md`  
**Verification state:** product implementation left unchanged; this report is the only product-tree addition

## Verdict

**Passed — M3 is accepted and may advance to M4 planning.**

All requirements M3-001 through M3-010 are implemented. The four findings in
the original independent report are closed. Fresh portable, quality,
sanitizer, projection, fingerprint, ARM64 system-OVS, retained M1, cleanup, and
document-render gates passed. No new product finding was identified.

The original failed report was preserved byte-for-byte with SHA-256
`2e6b5e90d93643397283fb7db2463940fba6b1f4f80165542186f0e19a7e6103`.

## Remediation closure

| Original finding | Independent result | Evidence |
|---|---|---|
| P1 dangling state-file symlink overwrite | **Closed** | Create returned nonzero for dangling and existing-target state symlinks. The symlink, link target, and target contents were preserved; no per-graph lock or OVS bridge was created. Source uses no-follow `symlink_status` checks and atomic `link(2)` no-replace initial publication. |
| P1 graph marker omitted from ownership | **Closed** | Changing only `graphx_graph` made status return 2 and destroy refuse before either bridge was deleted. Status, rollback/recovery discovery, preflight, and the same-transaction delete predicate all require the recorded graph ID. |
| P2 missing-state status mutated state | **Closed** | Status returned 2 against both an absent root and an existing empty root. It created neither a directory nor a lock. Existing status state is opened read-only with no-follow semantics and a shared non-blocking lock. |
| P2 stale architecture claims | **Closed** | Maintained Markdown and editable DOCX state that M3 owns OVS bridges only while ports, veth, namespaces, TAP, flows, routes, faults, mirrors, and captures remain deferred. The DOCX rendered as 23 clean pages. |

## Acceptance matrix

| ID | Derived requirement | Result | Independent evidence |
|---|---|---|---|
| M3-001 | Secure persistent state root/files, atomic publication, and symlink refusal | **Implemented** | Exact 0700 root and 0600 state/lock modes passed. Root symlinks, dangling file symlinks, existing-target file symlinks, permissive roots/files, and malformed state failed closed. Initial no-replace publication preserved existing directory entries. |
| M3-002 | Literal digest, graph ID, random token, lifecycle state, expected set, and stable UUIDs | **Implemented** | Live ledger matched the literal SHA-256, graph ID, `ready` state, ordered two-bridge expected set, and both OVS UUIDs. Tokens were 32 lowercase hex characters and changed between runs. |
| M3-003 | One non-blocking per-graph lifecycle lock | **Implemented** | A separately held exclusive lock caused status to fail immediately without mutation. Mutation uses an exclusive lock; inspection uses a shared lock; both reject malformed or symlink lock paths. |
| M3-004 | Atomic system-datapath creation and collision refusal | **Implemented** | Both bridges used `datapath_type=system` and owner, digest, and graph markers. A collision on the second declared bridge prevented creation of the first and preserved the collision UUID. |
| M3-005 | Per-mutation persistence, rollback, hard-crash discovery, recovery, and retry | **Implemented** | Failure points 1 and 2 rolled back to no bridges and no ledger. Crash points 1 and 2 exited 99 with respectively zero and one recorded UUID, recovery found every marker-owned mutation, and immediate create/destroy retry succeeded. |
| M3-006 | Complete-set preflight and atomic identity-conditioned deletion | **Implemented** | Unmarked and copied-marker replacements with changed UUIDs were preserved together with every owned sibling. Graph-marker-only drift also preserved the full set. The delete command places UUID plus owner, digest, and graph marker `wait-until` conditions in the same `ovs-vsctl` transaction as root-set removal and record destruction. |
| M3-007 | Create, status, destroy, recover, and non-mutating dry-run | **Implemented** | Normal and negative action paths passed. Missing and repeated cleanup failed closed, ready-state recovery refused, missing owned resources were tolerated, and status/dry-run created no state. |
| M3-008 | Future identity extension points without fabricated M3 resources | **Implemented** | The resource model exposes ifindex, namespace inode, container ID, TAP owner, route, rule, qdisc, capture, and process identity slots. The M3 ledger persisted only bridge UUIDs. |
| M3-009 | Bridge-only M3 boundary with no legacy or endpoint realization | **Implemented** | V2 dry-run emitted one state comment and exactly two `add-br` plans. It emitted no Docker network, port, veth, namespace, TAP, mirror, route, fault, capture, QEMU, or process mutation. Live version-2 route and fault requests failed closed. |
| M3-010 | V1 compatibility, complete gates, real OVS evidence, cleanup, and consistent documentation | **Implemented** | All five M0 hashes matched; projections and documentation checks passed; the full portable and Lima matrices passed; retained M1 snapshots matched; final guest state matched its empty initial OVS/namespace/M3 state. |

## Portable and static evidence

### Fresh builds

Fresh host build roots were created under `/var/tmp` for quick, C++23, C++20,
and quality verification.

- Quick: 39 of 39 tests passed in 31 seconds. Log:
  `outputs/verification/20260909T235218Z-quick.log`.
- Portable: C++23 and C++20 each passed 39 of 39 tests; every checked-in
  topology, local TCP/shared-memory/UDP/control/HTTPS exercise, 77 telemetry
  tests, 16 browser-console tests, and the production web build passed in 87
  seconds. Log: `outputs/verification/20260909T235043Z-portable.log`.
- Quality: formatting for 49 C++ files, clang-tidy, and cppcheck passed in 20
  seconds. Log: `outputs/verification/20260909T235300Z-quality.log`.
- Sanitizers: the supported macOS LLVM 21 UBSan profile passed all 39 enabled
  tests plus its coverage contract in 30 seconds. The package test is
  intentionally disabled in this profile. Log:
  `outputs/verification/20260909T235327Z-sanitizers.log`.
- `git diff --check`, projection drift, configuration-v2, portable ownership,
  Lima-static, and documentation consistency checks passed.

### Frozen M0 fingerprints

| Configuration | Observed SHA-256 |
|---|---|
| `examples/macvlan/graphx.yaml` | `40e09c1cb7ed78b118efeb10dbfe42fc8b06eb10312091f4df7afd9f0920f69d` |
| `examples/ipvlan-l2/graphx.yaml` | `da18ff3dfd11a72a834dd6933e161db0a18b92a1ddfc352340208fd274f40f37` |
| `examples/ipvlan-l3/graphx.yaml` | `598abfa52b274b8d6a3362bc67b141f0852dc590a61b552d99c5960d3b06864b` |
| `examples/mixed-network/graphx.yaml` | `ede943d05d0e70cd4bb36970bf94be9ea81787568f313766a807a3c14b6b65fc` |
| `examples/static-route-policy/graphx.yaml` | `33c53b043727d2404f1dcf4a8186dd1ce0b92536b49ead19f20416a166a355db` |

Every value exactly matches `migration_m0_baseline.md`.

## Lima ARM64 system-OVS evidence

Lima began and ended as `graphx|Running|aarch64|vz`. A fresh guest build at
`/var/tmp/graphx-m3-reverify-build` used GCC 13 on Ubuntu ARM64 and Open vSwitch
3.3.9. The checked-in privileged regression passed, followed by a separately
written verifier covering 15 live groups:

1. read-only missing-state status;
2. state-root symlink refusal;
3. dangling and existing-target state-file symlink preservation;
4. malformed and permissive state refusal;
5. complete collision preflight;
6. normal lifecycle, ledger identity, exact modes, repeated create, and locking;
7. graph-marker identity and complete-set preservation;
8. configuration drift and random-token renewal;
9. missing owned resource handling;
10. unmarked replacement preservation;
11. copied-marker replacement and UUID preservation;
12. in-process rollback after both bridge mutations;
13. hard-crash recovery and immediate retry after both bridge mutations;
14. ready recovery and repeated cleanup refusal; and
15. deferred route and fault refusal.

The retained M1 verifier then passed at
`/var/lib/graphx/m1/evidence/20260910T000022Z-1cafbb99`. Its `links.json`,
`routes.json`, `namespaces.txt`, and `ovs-bridges.txt` before/after files compare
byte-identically. Final inspection found no OVS bridges, network namespaces,
default M3 state entries, or bridges carrying a GraphX owner marker.

One preliminary CTest selection unintentionally included the privileged live
test in an unprivileged subset. It refused execution before mutation as
designed. The corrected unprivileged selection and both privileged verifiers
then passed; this was a verifier harness error, not a product failure.

## Documentation and scope

The operator, security, upgrade, support, configuration-v2, roadmap, and
architecture documents consistently describe M3 as a bridge-only ownership
boundary. The existing `GraphX_Architecture.docx` was rendered read-only with
the bundled document runtime. All 23 pages were visually inspected at full
resolution; no clipping, overlap, missing glyph, table break, or stale M2 scope
statement was found.

M4 remains responsible for container and namespace veth attachment. M6 remains
responsible for QEMU TAP. Profile flows, mirrors, routes, faults, captures, and
diagnostics remain later work and were not credited to M3.
