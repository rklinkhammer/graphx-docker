# Migration M5 independent verification

**Verdict:** CHANGES REQUIRED — M5 is not accepted; do not advance to M6.  
**Date:** 2026-09-09  
**Scope:** Independent verification only; no product fixes were made.

## Findings

### M5-F01 — P1: the shipped Lima environment rejects the default ownership state directory

`infrastructure/lima/provision.sh:48` creates `/var/lib/graphx/runs` with mode
0755, while `src/ownership.cpp:351-352` refuses any existing state root whose
mode is not 0700. The common M5 wrapper invokes `graphx infra create` without a
`--state-dir`, so it necessarily selects that incompatible directory.

This was reproduced by running the actual mixed-network M5 launcher in the
configured ARM64 Lima VM with a fresh GraphX build. Compose built the image and
started its three management-connected containers, after which infrastructure
creation failed with:

```text
graphx: existing ownership state root permissions must be 0700
```

The wrapper removed its containers and management network and no OVS bridge or
router namespace remained. Nevertheless, M5-009 and the defining native
Linux/Lima parity requirement fail because the supported launcher cannot start
in the supplied VM.

### M5-F02 — P1: hard-crash recovery after mirror creation cannot pass its own identity check

A verifier-only run set `GRAPHX_M5_CRASH_AFTER=8`, which terminates immediately
after the first mirror endpoint mutation and before its ledger update. The
subsequent `infra recover` correctly discovers the host veth, OVS Port,
Interface, and Mirror row, but `src/ownership.cpp:1617-1619` records the host
ifindex in `peer_ifindex`. Mirror cleanup later requires the actual peer ifindex
at `src/ownership.cpp:1648-1653`, so the unchanged owned resource is rejected:

```text
graphx: refusing replaced mirror peer: m5-left-span
```

Recovery after namespace publication (`GRAPHX_M5_CRASH_AFTER=3`) passed. Normal
mirror lifecycle and same-marker Mirror UUID replacement refusal also passed.
The mutation-8 failure is deterministic and violates M5-008 and the inherited
identity-safe recovery contract. The test harness cleaned the test-scoped
resources afterward; no bridge, namespace, link, Mirror row, container, or
ledger residue remained.

### M5-F03 — P1: static-route-policy and external-SDR are configurations, not migrated runnable labs

The M5 plan requires the focused static-route-policy and SDR external examples
to use the common realization. Both have a new `graphx-ovs.yaml`, but neither
has an M5 launcher or Compose selection for it. Their existing launchers still
select `graphx.yaml` and build the complete version-1 infrastructure. The M5
handoff and READMEs explicitly state that these launchers remain compatibility
workflows until future external adapters switch configurations.

External attachment non-adoption is correct, but it does not satisfy the lab
migration exit gate: an external adapter must create only its explicitly owned
boundary while GraphX creates the shared M5 infrastructure. M5-009 and M5-010
therefore remain incomplete.

### M5-F04 — P2: M5 dry-run omits material namespace, policy, route, and profile-flow mutations

All six `graphx-ovs.yaml` files validate and their dry-runs avoid Docker
data-plane networks. However, the mixed-network plan prints veth and mirror
summaries without planning `ip netns add`, forwarding, nftables rules, endpoint
routes, or the `ovs-ofctl` profile flows that real creation performs. This is
not a complete, reviewable realization plan and leaves M5-002 only partially
satisfied.

## Acceptance matrix

| ID | Acceptance criterion | Result |
|---|---|---|
| M5-001 | Fresh portable, quality, sanitizer, projection, documentation, M2/M3/M4 gates | PASS |
| M5-002 | Six complete OVS-only migrated plans | FAIL — configurations validate, but dry-run omits material mutations |
| M5-003 | Frozen version-1 behavior and exact fingerprints | PASS |
| M5-004 | Real Lima identity ledger and repeated exact lifecycle | PASS — two invocations, four clean cycles |
| M5-005 | Routed packet, named nftables policy counters, and SPAN packet evidence | PASS |
| M5-006 | MACVLAN distinct identity and IPvlan shared identity/steering/isolation | PARTIAL — declarations pass; shared identity and real broadcast drop pass; actual macvlan launcher is blocked by F01 |
| M5-007 | Namespace/mirror/endpoint collision and replacement preservation | PASS for tested namespace, late mirror-peer, Mirror UUID, and inherited M4 cases |
| M5-008 | Namespace, endpoint, and mirror interruption/recovery | FAIL — mirror recovery fails after mutation 8 |
| M5-009 | Management-only Compose and common native-Linux/Lima launch path | FAIL — Compose models validate, but actual Lima start fails; two labs have no M5 launcher |
| M5-010 | External ownership and M6/M7/legacy boundaries are accurate | PARTIAL — boundaries are accurate, but route/SDR migrations are unfinished |

## Passing evidence

- A completely fresh macOS debug build passed all 41 tests, including package,
  projections, documentation, M2, M3, M4, and M5 portable contracts.
- A completely fresh sanitizer build passed all 41 enabled tests; the packaging
  test was intentionally disabled under sanitizers.
- A completely fresh clang-tidy/cppcheck build passed after supplying the
  installed Homebrew `clang-tidy` path explicitly.
- All six M5 configurations validated. All four management-only Compose models
  passed `docker compose config --quiet` and declare no Docker macvlan/ipvlan or
  external data-plane network.
- The five frozen M0 version-1 dry-run SHA-256 values matched exactly.
- A fresh GNU/Linux build in the ARM64 Lima VM passed the M3 live regression and
  two invocations of the M5 live regression, totaling four normal lifecycle
  cycles.
- Independent live hooks verified router namespace inode/alias behavior,
  shared IPvlan MAC identity, destination-IP/ARP steering, a real broadcast
  packet incrementing the OVS drop-flow counter, routed ping, nonzero nftables
  counters in both directions, SPAN receive growth, namespace and late-peer
  collision refusal before sibling mutation, and same-marker Mirror UUID
  replacement preservation.

## Required remediation

1. Make the provisioned default state directory and ownership permission
   contract agree, then prove the actual `ovs-up/status/down` workflow in Lima.
2. Recover the real mirror peer ifindex after a mutation-before-ledger crash and
   add a privileged regression for that exact interruption point.
3. Provide M5 route-policy and SDR external adapters/launchers that select
   `graphx-ovs.yaml` and own only their declared external boundary.
4. Make dry-run enumerate every material M5 mutation, including namespace
   creation, forwarding, routes, nftables policy, semantic flows, and mirrors.
5. Repeat the full M5 verification matrix and exact cleanup audit. Do not begin
   M6 until the result is accepted.

All verifier-created containers, links, namespaces, bridges, Mirror rows, test
image, guest build tree, and host temporary build trees were removed. The only
repository change from verification is this report.
