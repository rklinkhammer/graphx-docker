# Migration M5 final independent verification

**Verified:** 2026-09-10 (America/New_York)  
**Repository:** `~/workspace/graphx-docker`  
**Branch/base:** `main` at `46827c9a95a65cbc793d163500a5ca2d25835727`  
**Product:** GraphX 1.1.0  
**Scope:** Verification only; no product files were changed by this pass.

## Verdict

**ACCEPTED — M5 satisfies M5-001 through M5-010 and may advance to M6.**

The final remediation closes all three findings from
`migration_m5_reverification.md`. Version-2 manual route apply/clear works and
changes real packet delivery; bridge complete-set validation refuses an
unrecorded OVS port before changing any owned sibling or ledger; and the
maintained Markdown and DOCX architecture artifacts now describe M5 as
implemented while retaining the M6 TAP and M7 capture/fault boundaries.

## Acceptance matrix

| ID | Derived requirement | Result | Independent evidence |
|---|---|---|---|
| M5-001 | Fresh portable, quality, sanitizer, projection, documentation, M2/M3/M4, format, and compatibility gates | **PASS** | Fresh macOS debug and quality trees each passed 41/41 tests. Clang-tidy, cppcheck, LLVM 21 formatting, and `git diff --check` were clean. A fresh LLVM 21 UBSan tree passed all 41 enabled tests plus sanitizer coverage; packaging was intentionally disabled only in that profile. |
| M5-002 | Six complete OVS-only migrated plans | **PASS** | All six `graphx-ovs.yaml` files validated. Their plans contain the applicable system-OVS bridges, veths, namespaces, forwarding, routes, nftables rules, mirrors, and semantic flows, with no Docker data-plane network command. |
| M5-003 | Frozen version-1 behavior and fingerprints | **PASS** | All five frozen dry-run SHA-256 values matched exactly; the legacy configurations and workflows remain present. |
| M5-004 | Repeated real ARM64 Lima identity ledger and exact lifecycle | **PASS** | A fresh ARM64 `vz` Lima build passed 41/41. M3 and M4 privileged regressions passed. Two separate M5 live invocations completed four normal create/status/destroy cycles and checked container IDs, namespace inode, interface aliases/indices, Bridge/Port/Interface UUIDs, Mirror UUIDs, and cleanup. |
| M5-005 | Routed packets, named nftables policy, manual route transition, and SPAN evidence | **PASS** | The live regression routed packets across the namespace and observed semantic flows. The focused route lab proved named allow delivery, named deny timeout with a nonzero drop counter, absence of the manual route, successful `apply-route` delivery to `10.64.30.10`, and successful `clear-route` removal. OVS/SPAN counters were nonzero in the mixed lab. |
| M5-006 | MACVLAN distinct identity and IPvlan shared identity, steering, and isolation | **PASS** | The migrated MACVLAN lab realized its three declared distinct endpoint identities. The repeated M5 packet regression and migrated mixed/IPVLAN plans use the shared IPvlan identity, destination-IP/ARP steering flows, routed delivery, and a non-ARP broadcast/multicast drop flow. The previously passing focused MAC/flow probes are unaffected by the final remediation. |
| M5-007 | Namespace, mirror, endpoint, and bridge complete-set collision/replacement preservation | **PASS** | Fresh M3/M4 tests retained bridge, container, Port, Interface, and host-link replacement coverage. The M5 live regression added an unrecorded OVS port, proved destroy refused with `unexpected ports`, compared the ledger byte-for-byte, preserved both bridges, the router namespace, all sibling links, and the original Mirror UUID, then removed the external port and completed cleanup. Previously passing namespace, late mirror-peer, and same-marker Mirror-row replacement probes remain applicable. |
| M5-008 | Namespace, endpoint, and mirror interruption/recovery | **PASS** | Each M5 live invocation injected hard failure after mutations 3 and 8, recovered successfully, and left no bridge, namespace, link, Mirror row, or ledger before proceeding with two clean lifecycle cycles. The complete-set refusal is now non-mutating and cleanup resumes after the external port owner detaches it. |
| M5-009 | Management-only Compose and common native-Linux/Lima launch paths | **PASS** | All five Compose models rendered successfully and declare only management connectivity. The mixed, MACVLAN, IPVLAN-L2, IPVLAN-L3, static-route, and external-SDR launchers exercised the common ARM64 Lima/system-OVS path and completed ownership-safe teardown. |
| M5-010 | Accurate external ownership, legacy simulation, M6 TAP, and M7 capture/fault boundaries | **PASS** | External route/SDR adapters create and remove only their explicit boundary resources; GraphX does not adopt them. Docker Desktop OVS remains documented as a legacy simulation, QEMU TAP remains M6, and declarative capture/fault lifecycle ownership remains M7. |

## Remediation closure

### M5-R01 closed — version-2 manual route lifecycle

`graphx infra route apply` now accepts a validated version-2
`linux_namespace` router and exact declared destination. The focused launcher
exposes `apply-route` and `clear-route`. The route was absent after `up`, the
receiver timed out before application, the exact kernel route appeared after
application, the one-way UDP receiver then accepted the expected token, and
the route disappeared after clear.

### M5-R02 closed — bridge complete-set preflight

The destroy preflight now compares the live Bridge Port UUID set with the
internal port plus every still-present ledger endpoint before the first
deletion. A verifier-added external port caused an immediate refusal. The
ledger, bridges, namespace, endpoint links, and Mirror identity remained
unchanged. After the verifier removed its port, normal GraphX destroy completed.

### M5-R03 closed — maintained architecture artifacts

The documentation consistency test passes. `GraphX_Architecture.docx` was
rendered to 24 pages and every page was visually inspected. No clipping,
overlap, broken table, missing glyph, or page-layout defect was observed. The
roadmap states that M5 implements router namespaces, namespace veths, mirrors,
policy, IPvlan flows, and migrated labs, while TAP and declarative capture/fault
ownership remain M6 and M7 respectively.

## Compatibility fingerprints

| Configuration | Observed SHA-256 |
|---|---|
| `examples/macvlan/graphx.yaml` | `40e09c1cb7ed78b118efeb10dbfe42fc8b06eb10312091f4df7afd9f0920f69d` |
| `examples/ipvlan-l2/graphx.yaml` | `da18ff3dfd11a72a834dd6933e161db0a18b92a1ddfc352340208fd274f40f37` |
| `examples/ipvlan-l3/graphx.yaml` | `598abfa52b274b8d6a3362bc67b141f0852dc590a61b552d99c5960d3b06864b` |
| `examples/mixed-network/graphx.yaml` | `ede943d05d0e70cd4bb36970bf94be9ea81787568f313766a807a3c14b6b65fc` |
| `examples/static-route-policy/graphx.yaml` | `33c53b043727d2404f1dcf4a8186dd1ce0b92536b49ead19f20416a166a355db` |

## Environmental observations

- The first macOS sanitizer configure omitted the Command Line Tools SDK and
  failed while locating standard headers. A new, clean tree with the explicit
  SDK sysroot passed; this was a verifier fixture issue, not a product defect.
- One repeated Compose build hit a Docker registry metadata timeout. The
  already completed launcher cycle and subsequent cached builds passed; the
  timeout was external and did not affect acceptance.
- Docker Compose warned that standalone buildx was absent, but its integrated
  builder successfully built the pinned Dockerfile and all exercised launchers.

## Cleanup

All verifier-created containers, bridges, Mirror rows, links, namespaces,
ledgers, lock files, external-adapter state, test images, guest build/log trees,
and host out-of-tree build/render artifacts were removed. The final residue
audit found no GraphX laboratory bridge, namespace, container, or ownership
ledger. Historical M5 reports were retained unchanged; this final report is the
only repository addition from this verification pass.
