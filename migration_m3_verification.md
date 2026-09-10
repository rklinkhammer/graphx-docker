# Migration M3 independent verification

**Verified:** 2026-09-09 (America/New_York)  
**Repository:** `~/workspace/graphx-docker`  
**Branch/base:** `main` at `b16c9631ffac7151d9b72ae634b8838f7515b606`  
**Product:** GraphX 1.1.0  
**Verification state:** product implementation left unchanged; this report is the only product-tree addition

## Verdict

**Failed — remediation required before M4.**

M3's main bridge lifecycle works and most adversarial checks passed. Fresh
portable, quality, sanitizer, projection, fingerprint, live ARM64 system-OVS,
and retained M1 regression gates passed. Normal create/status/destroy,
collision refusal, locking, exact-mode state, rollback, hard-crash recovery,
immediate retry, configuration drift, missing resources, and UUID-based
replacement preservation behaved correctly.

Acceptance nevertheless fails because create overwrites a dangling ownership
state symlink, and `graphx_graph` is not part of bridge health or the atomic
delete predicate. Status also creates state artifacts despite being documented
as read-only, and the maintained architecture document contains contradictory
pre-M3 statements.

## Findings

### [P1] M3-001: dangling state-file symlink is overwritten

`create` checks `std::filesystem::exists(state_path)`. That returns false for a
dangling symlink. `save_state` then renames its temporary regular file over the
symlink and bridge creation proceeds. In the real Lima guest:

```text
dangling-state-symlink:FAIL-overwritten-and-created rc=0 remains_symlink=no
```

The test began with a mode-0700 state root containing
`mixed-network.yaml -> <nonexistent target>`. Expected behavior was a nonzero
refusal with the symlink preserved and no OVS mutation. Actual behavior was
success, replacement of the symlink by a regular ledger, and creation of both
bridges. Cleanup subsequently removed the test bridges and ledger.

The relevant path is `src/ownership.cpp:366`: the existence check follows
symlink semantics, while the rename at `src/ownership.cpp:225` replaces the
directory entry. This contradicts the non-symlink state-file requirement and
`docs/ownership-m3.md:68`.

Required remediation: inspect the state pathname without following links and
refuse every existing directory entry, including dangling symlinks, before any
state publication or OVS mutation. Add a portable/static contract and a live
Lima regression proving the symlink and unrelated target are preserved.

### [P1] M3-006: graph ownership marker is not verified or conditioned on delete

Every bridge is created with three M3 markers: `graphx_owner`,
`graphx_config_hash`, and `graphx_graph`. The ownership predicate checks only
UUID, owner token, and configuration digest (`src/ownership.cpp:314`), and the
atomic OVSDB delete waits on only owner token and digest
(`src/ownership.cpp:320`).

In the real guest, changing only the graph marker produced:

```text
changed-graph-marker status_rc=0 deleted=yes
```

Expected behavior was unhealthy status and cleanup refusal. Actual behavior
reported a healthy bridge and deleted it. UUID and the other two markers were
unchanged.

Required remediation: include `external_ids:graphx_graph=<recorded graph ID>`
in status/recovery/preflight ownership checks and in the same OVSDB transaction
that conditions removal. Add a live test that changes only this marker and
proves the bridge and every owned sibling remain untouched.

### [P2] M3-007: missing-state status is not read-only

`docs/ownership-m3.md:43` says status is read-only. The lifecycle creates and
secures the state root and opens a persistent lock before checking whether the
ledger exists (`src/ownership.cpp:354`). Against a previously nonexistent test
root, missing-state status returned 2 but left both a mode-0700 directory and a
mode-0600 `mixed-network.lock`:

```text
status_rc=2 root_exists=yes lock_exists=yes
```

No OVS resource was changed, so this is lower severity than the ownership
failures. Required remediation is either to make status genuinely read-only
using no-create/no-follow opens, or to correct the operator contract and make
the state mutation explicit. Read-only behavior is preferable.

### [P2] M3-010: maintained architecture text contradicts implemented M3 scope

`docs/GraphX_Architecture.md:19` and the M3 roadmap correctly describe owned
bridge realization. However, line 29 still says version-2 OVS realization is
not implemented, and lines 37–40 still say version-2 infrastructure commands
fail closed. Those statements now contradict the CLI and the rest of the same
document.

Required remediation: state that M3 realizes only owned OVS bridges while
ports, veth, namespaces, TAP, flows, routes, faults, and capture remain
deferred. Regenerate the editable DOCX and rerun visual/document consistency
checks.

## Acceptance matrix

| ID | Derived requirement | Result | Evidence |
|---|---|---|---|
| M3-001 | Secure persistent state root/files, atomic publication, no symlinks | **Partial** | Exact 0700/0600 modes, atomic publication, root symlink, existing-target file symlink, permissive and malformed state passed; dangling file symlink was overwritten. |
| M3-002 | Literal digest, graph identity, random token, lifecycle state, expected set, stable UUID records | **Implemented** | Live ledger matched the literal SHA-256, graph ID, `ready` state, two expected bridges, two stable UUIDs, and a 32-hex random token. A second create used a different token. |
| M3-003 | One non-blocking per-graph mutation lock | **Implemented** | A separately held exclusive lock caused status to fail immediately; resources were unchanged. Lock mode was 0600. |
| M3-004 | Atomic system-datapath bridge creation and collision refusal | **Implemented** | Both bridges used `datapath_type=system` and all three creation markers. An unowned second-name collision prevented creation of the first bridge and preserved the collision UUID. |
| M3-005 | Per-mutation state, rollback, hard-crash discovery, recovery, immediate retry | **Implemented** | Failure points 1 and 2 rolled back exactly. Crash points 1 and 2 exited 99, left 0 and 1 UUID records respectively, recovered all marker-owned bridges, and allowed immediate create/destroy retry. |
| M3-006 | Complete-set preflight and atomic identity-conditioned deletion | **Partial** | Replacements of both bridges, including copied owner/digest/graph markers with a different UUID, were preserved with their owned sibling. UUID/owner/digest are atomic delete conditions, but `graphx_graph` is omitted and a changed graph marker was deleted. |
| M3-007 | Create/status/destroy/recover and non-mutating dry-run | **Partial** | All actions and dry-runs work; dry-run created no state. Missing-state status creates a state root and lock despite the read-only documentation. |
| M3-008 | Explicit future identity extension points without fabricated resources | **Implemented** | The resource model exposes ifindex, namespace inode, container, TAP, route, rule, qdisc, capture, and process identity slots; the M3 ledger persisted only OVS bridge UUIDs. |
| M3-009 | Bridge-only M3 boundary; no legacy or endpoint mutation | **Implemented** | V2 dry-run emitted exactly two `add-br` transactions and no Docker network, port, veth, namespace, TAP, mirror, route, fault, capture, or process command. Route/fault requests failed closed. |
| M3-010 | V1 compatibility, gates, runtime evidence, and consistent documentation | **Partial** | All executable gates and five M0 fingerprints passed; architecture prose contains two stale pre-M3 claims. |

## Portable and static evidence

### Fresh out-of-tree portable build

The verifier created a new host build root under `/var/tmp` and removed it
afterward. The portable profile completed in 86 seconds:

- C++23: 39/39 tests passed;
- C++20: 39/39 tests passed;
- every checked-in topology validated and dry-ran;
- local TCP, shared-memory, UDP, control, HTTPS, and observation checks passed;
- telemetry: 77/77 tests passed;
- web console: 16/16 tests passed and the production build completed.

Log: `outputs/verification/20260909T231251Z-portable.log`.

The independent quick profile also passed 39/39 tests in 28 seconds. Log:
`outputs/verification/20260909T232638Z-quick.log`.

### Quality and sanitizers

- A fresh quality build passed formatting for 49 C++ files, clang-tidy, and
  cppcheck in 20 seconds. Log:
  `outputs/verification/20260909T231434Z-quality.log`.
- The supported macOS LLVM 21 UBSan profile passed all 39 enabled tests plus
  sanitizer coverage in 29 seconds. The package test is intentionally disabled
  in this profile. Log:
  `outputs/verification/20260909T231503Z-sanitizers.log`.

### Compatibility and scope

The five exact version-1 plan digests matched `migration_m0_baseline.md`:

| Configuration | Observed SHA-256 |
|---|---|
| `examples/macvlan/graphx.yaml` | `40e09c1cb7ed78b118efeb10dbfe42fc8b06eb10312091f4df7afd9f0920f69d` |
| `examples/ipvlan-l2/graphx.yaml` | `da18ff3dfd11a72a834dd6933e161db0a18b92a1ddfc352340208fd274f40f37` |
| `examples/ipvlan-l3/graphx.yaml` | `598abfa52b274b8d6a3362bc67b141f0852dc590a61b552d99c5960d3b06864b` |
| `examples/mixed-network/graphx.yaml` | `ede943d05d0e70cd4bb36970bf94be9ea81787568f313766a807a3c14b6b65fc` |
| `examples/static-route-policy/graphx.yaml` | `33c53b043727d2404f1dcf4a8186dd1ce0b92536b49ead19f20416a166a355db` |

`graphx project graphx.yaml --check --output-dir config` reported all
projections current. The documentation consistency CTest passed, but it does
not detect the contradictory architecture prose identified above.

## Lima ARM64 system-OVS evidence

The verifier built the current tree from a fresh guest out-of-tree directory
using GCC 13 on Ubuntu ARM64 and Open vSwitch 3.3.9. Tests used only disposable
state roots and the exact configured names `br-gx-mac` and `br-gx-ipv`.

Passed live cases:

- normal create/status/destroy;
- state-root, state-file, and lock modes;
- literal digest, random token, expected set, stable UUIDs, system datapath,
  and all three creation `external_ids`;
- repeated create and cleanup refusal;
- missing state and unowned-name collision;
- permissive root/file and empty malformed state;
- state-root symlink and state-file symlink with an existing target;
- configuration drift and missing owned resource;
- concurrent graph lock;
- rollback after bridge mutations 1 and 2;
- hard crash after bridge mutations 1 and 2;
- unrecorded atomic-mutation discovery, recovery, and immediate retry;
- first-bridge replacement without copied markers;
- second-bridge replacement with copied markers and a different UUID;
- complete-set preflight preserving the replacement and owned sibling;
- refusal to use `recover` on a ready run.

Failed live cases are the dangling state symlink and changed graph marker
described in Findings.

The retained M1 verifier passed and recorded exact before/after state at:

```text
/var/lib/graphx/m1/evidence/20260909T232346Z-7835f3e1
```

Its link, route, OVS bridge, and namespace snapshots compare identically. The
independent host and guest build roots and every test bridge/ledger were
removed. Lima began and ended `Running` on ARM64/vz. Final inspection found no
OVS bridges, namespaces, default M3 ledger entries, or GraphX-owned bridges.

## Remediation gate

Do not advance to M4 until the two P1 ownership findings are fixed and all four
findings are covered by regressions. After remediation, rerun at minimum:

1. fresh quick, portable, quality, sanitizer, projection, documentation, and
   M0 fingerprint gates;
2. the full Lima M3 matrix, including dangling symlink preservation and
   graph-marker-only drift;
3. both replacement cases with complete-set sibling preservation;
4. retained M1 verification and final unrelated-state comparison;
5. architecture DOCX regeneration, rendering, and visual inspection.
