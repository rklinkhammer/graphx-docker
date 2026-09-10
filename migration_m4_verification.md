# Migration M4 independent verification

**Verified:** 2026-09-09 (America/New_York)  
**Repository:** `~/workspace/graphx-docker`  
**Branch/base:** `main` at `3534b1ca9a29bd8a059292b78e448537eeabc89f`  
**Product:** GraphX 1.1.0  
**Verification state:** product implementation left unchanged; this report is the only product-tree addition

## Verdict

**CHANGES REQUIRED — M4 is not accepted and must not advance to M5.**

The primary M4 lifecycle works. Fresh portable, quality, sanitizer, projection,
documentation, migration, M3 regression, and frozen-fingerprint gates passed.
Real ARM64 Lima tests proved Docker label/image/container/namespace identity,
owned veth and OVS realization, endpoint settings, two lifecycle cycles, restart
detection, explicit reattachment, name-collision refusal, ordinary rollback,
hard-crash recovery, and immediate retry.

Acceptance fails because a same-named replacement OVS Port carrying copied
GraphX markers but a different UUID is not recognized as a replacement. Destroy
returns success and deletes that unrelated replacement with the owned bridge.
The maintained architecture source also retains one stale statement that all
endpoint realization is deferred.

## Findings

### [P1] M4-007: copied-marker OVS Port replacement is deleted

The ledger records the original Port UUID. The verifier removed that Port and
created a new same-named Port and Interface on the owned bridge with copied
`graphx_owner`, `graphx_attachment`, `graphx_config_hash`, and `graphx_graph`
markers. The replacement UUID differed from the ledger UUID.

Observed in the Lima guest:

```text
replacement_destroy_rc=0
replacement_preserved=false
DEFECT: copied-marker replacement OVS Port was not preserved
```

The preflight at `src/ownership.cpp:1050` checks endpoint ownership only when
the recorded Port UUID still exists. When it does not, the same-named replacement
is not inspected. `delete_owned_endpoint` similarly skips OVS deletion when the
recorded UUID is absent (`src/ownership.cpp:594`), then the later owned-bridge
deletion removes the replacement Port and Interface as children of that bridge.

Expected behavior is a nonzero refusal before any sibling mutation, with the
replacement Port/Interface, host veth, bridge, and ownership ledger preserved.
Required remediation is to inspect the expected OVS name as well as the recorded
UUID. If the name resolves to a different UUID, cleanup must refuse regardless
of copied markers. Add a privileged regression that proves complete-set
preservation for Port replacement and repeats the same check for Interface UUID
replacement.

### [P2] M4-010: architecture achievement table still says endpoints are deferred

Most M4 documentation accurately distinguishes implemented container veth from
deferred namespace veth, TAP, profile flows, mirrors, general policy, faults,
and capture. However, `docs/GraphX_Architecture.md:79` still says “endpoint
realization remains deferred” in the current achievement table. That contradicts
the implemented M4 boundary described at lines 19, 29, 37-42, and 171.

Required remediation is to identify managed-container veth realization as M4 in
that table while retaining the later boundaries, then regenerate and visually
check the editable DOCX.

## Acceptance matrix

| ID | Derived requirement | Result | Independent evidence |
|---|---|---|---|
| M4-001 | Fresh portable, quality, sanitizer, migration, M3, projection, documentation, and fingerprint gates | **Implemented** | Fresh debug build passed 40/40 tests; fresh clang-tidy/cppcheck build passed; fresh sanitizer build passed all 40 enabled tests plus sanitizer coverage; package is intentionally disabled only in the sanitizer profile. |
| M4-002 | M4 dry-run content and strict phase boundary | **Implemented** | Mixed-network v2 dry-run declared project/service selection, host/peer veth names, OVS bridge/port, address, and MTU. It contained no Docker network, legacy MACVLAN/IPVLAN driver, namespace, TAP, mirror, route mutation, fault, capture, or process mutation. |
| M4-003 | Resolve exactly one verified Compose service and persist full container/namespace identity | **Implemented** | Live selection used exact project/service labels and image. Ledger contained the full 64-character container ID and the observed `/proc/<pid>/ns/net` inode; status re-resolution passed. |
| M4-004 | Own and verify veth/OVS identities and endpoint MAC/address/MTU/routes | **Implemented** | Live ledger recorded both ifindices and OVS Port/Interface UUIDs. Host/peer aliases and all intrinsic markers passed status. Container inspection proved MAC `02:79:00:00:00:02`, `10.79.0.2/24`, MTU 1400, and route `10.80.0.0/24 via 10.79.0.1 dev gxdata0`. |
| M4-005 | Two clean cycles with management separate from OVS data plane | **Implemented** | The checked-in live test completed create/status/destroy, replacement/reattach, and a second status/destroy. Docker management remained available through `docker exec`; GraphX created no Docker network and attached only the additional data veth to system OVS. |
| M4-006 | Restart/replacement detection and explicit safe reattachment | **Implemented** | Replacing the service changed its full ID and namespace inode. Status returned 2 with `missing-replaced-or-restarted`; destroy/create attached the replacement and restored healthy status without silent adoption. |
| M4-007 | Collision and replacement refusal with complete-set preservation | **Partial — blocker** | Unowned host interface, target namespace interface, and OVS name collisions all refused before bridge/state publication. Host ifindex and Interface UUID identity are checked. A copied-marker Port replacement with a different UUID was deleted instead of preserved. |
| M4-008 | Failure rollback, hard-crash recovery, and immediate retry | **Implemented for the exposed endpoint mutation point** | `GRAPHX_M4_FAIL_AFTER=2` rolled back the one bridge and endpoint. `GRAPHX_M4_CRASH_AFTER=2` exited 99 after endpoint mutation; recover found intrinsic ownership, removed endpoint then bridge, and immediate create/destroy retry passed. No finer-grained crash point is exposed inside veth/OVS/move/configuration steps. |
| M4-009 | Deterministic M2 migration without invented addresses | **Implemented** | Two root-config migrations had identical SHA-256 `a782b28ff7a97f95060d25dc3e45ce35de51bc1ea724dac24db26579a7574e6b`. The address-less migrated file validated for review, while real M4 create failed before ledger publication with `M4 realization requires a declared address`. |
| M4-010 | Accurate scope documentation and deferred-boundary statements | **Partial** | Focused M4 docs and most architecture prose are accurate; the architecture achievement table retains a contradictory endpoint-deferred statement. |

## Portable and static evidence

The verifier used a new out-of-tree root at
`/tmp/graphx-m4-verify.0RurLc`:

- Debug build: 40 of 40 tests passed in 22.74 seconds, including package,
  configuration-v2, M3 ownership, M4 container-veth, projection behavior,
  documentation consistency, Lima static checks, and all portable examples.
- Quality build: Apple Clang C++20, clang-tidy, and cppcheck completed without a
  diagnostic.
- Sanitizer build: Apple Clang C++23 address/undefined-behavior profile passed
  all 40 enabled tests and sanitizer coverage in 30.27 seconds. The package test
  is intentionally disabled in that profile.
- `git diff --check`, schema parsing, projection drift, and documentation
  consistency passed.

All five version-1 dry-run hashes exactly matched the frozen M0 record:

| Configuration | Observed SHA-256 |
|---|---|
| `examples/macvlan/graphx.yaml` | `40e09c1cb7ed78b118efeb10dbfe42fc8b06eb10312091f4df7afd9f0920f69d` |
| `examples/ipvlan-l2/graphx.yaml` | `da18ff3dfd11a72a834dd6933e161db0a18b92a1ddfc352340208fd274f40f37` |
| `examples/ipvlan-l3/graphx.yaml` | `598abfa52b274b8d6a3362bc67b141f0852dc590a61b552d99c5960d3b06864b` |
| `examples/mixed-network/graphx.yaml` | `ede943d05d0e70cd4bb36970bf94be9ea81787568f313766a807a3c14b6b65fc` |
| `examples/static-route-policy/graphx.yaml` | `33c53b043727d2404f1dcf4a8186dd1ce0b92536b49ead19f20416a166a355db` |

## Lima ARM64 evidence and cleanup

The `graphx` Lima VM ran ARM64 Linux with rootful Docker and system Open vSwitch.
The verifier rebuilt the current guest tree, created a disposable Debian test
image with `iproute2`, and ran both the checked-in M4 live regression and an
independent adversarial probe. The ordinary test container did not receive
`NET_ADMIN`; the adversarial container received it only to create a deliberate
target-interface collision.

Passed live cases included two lifecycle cycles, exact service identity,
endpoint and route configuration, restart detection, explicit reattachment,
host/target/OVS collision refusal, ordinary failure rollback, hard-crash
recovery, immediate retry, missing-address refusal, and the retained M3 live
regression. The copied-marker Port replacement failed as documented above.

Final inspection found no targeted containers, veths, test bridges, state roots,
or test image. Lima remained running in its initial ARM64 `vz` configuration.

## Remediation gate

Do not advance to M5. After remediation, independently rerun at minimum:

1. copied-marker same-name Port replacement with a changed UUID, proving the
   replacement, Interface, host veth, bridge, ledger, and every sibling remain;
2. the corresponding Interface UUID replacement and host-ifindex replacement;
3. normal destroy after restoration plus failure/crash recovery and immediate
   retry;
4. fresh portable, quality, sanitizer, M0 fingerprint, M2, M3, projection, and
   documentation gates; and
5. architecture DOCX regeneration, render, and full-page visual inspection.
