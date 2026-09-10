# Migration M5 independent re-verification

**Re-verified:** 2026-09-09 (America/New_York)  
**Repository:** ~/workspace/graphx-docker  
**Branch/base:** main at 46827c9a95a65cbc793d163500a5ca2d25835727  
**Product:** GraphX 1.1.0  
**Prior verification SHA-256:** 5f8fd133f088bf716e693755c60fe2a2e31aec69e8fcba6772aaca77c121ec15  
**Verification state:** product implementation left unchanged; this report is the only repository addition from re-verification

## Verdict

**CHANGES REQUIRED — M5 is not accepted; do not advance to M6.**

The four previously reported remediation items are substantially closed: the
Lima state-directory mode agrees with the ownership contract, first-mirror
hard-crash recovery passes, both external-boundary launchers run, and dry-run
now exposes the material M5 mutations. Fresh host and ARM64 Linux suites pass,
normal wrappers cleanly cycle, semantic MAC/flow/policy/SPAN evidence passes,
and all frozen version-1 fingerprints remain exact.

Acceptance is blocked by two functional defects and stale architecture
documentation. The migrated static-route lab declares and plans a manual route
that the CLI refuses to apply for configuration version 2. Separately, an
adapter-owned external port on a GraphX bridge is not included in the
complete-set deletion preflight: direct GraphX destroy deletes owned endpoint
siblings before refusing the bridge and cannot subsequently resume cleanup.
The maintained Markdown and DOCX still describe namespace veth and profile-flow
realization as deferred despite M5 implementing them.

## Findings

### M5-R01 — P1: version-2 manual routes are declared but cannot be applied

examples/static-route-policy/graphx-ovs.yaml declares 10.64.30.10/32 with
install: manual, and M5 dry-run correctly reports it. The M5 launcher at
examples/static-route-policy/scripts/ovs-lab.sh:35 exposes only up, status, and
down. Invoking the documented route operation directly fails before mutation
because src/infra.cpp:294-297 rejects every version-2 route command:

~~~text
graphx: configuration version 2 route realization is deferred beyond M3
~~~

Before this failure, independent packet probes proved the named allow and deny
nftables counters advanced and all three SPAN peers received packets. The
kernel route was absent as declared. The required missing-route to applied-route
transition could not be completed through GraphX. This fails M5-005 and leaves
the focused M5 launcher incomplete under M5-009.

Required remediation: allow infra route apply and clear for a validated
version-2 Linux namespace router, retain the exact router/destination allowlist,
add the two actions to the M5 route launcher, and prove
absent/apply/delivery/clear using kernel-route and receiver evidence.

### M5-R02 — P1: an expected external bridge port causes partial sibling deletion

The SDR adapter attaches its explicitly owned boundary veth to br-sdr with
ovs-vsctl add-port at examples/external-ovs-boundary.sh:15-16. This is the
declared M5 external boundary and is intentionally absent from GraphX's
owned-resource ledger.

With the external SDR port still attached, direct GraphX destroy produced:

~~~text
- mirror_veth mirror-sdr
- container_veth sink-data
- container_veth processor-data
- ovs-vsctl del-br br-sdr
graphx: identity changed while deleting owned OVS bridge br-sdr
~~~

The external namespace, veth, bridge, and ledger were preserved, but both
managed-container veths and the mirror had already been deleted. A second
destroy then refused the now-missing mirror and could not complete; manual,
identity-scoped cleanup was required.

The cause is a preflight/deletion mismatch. The preflight at
src/ownership.cpp:1693-1721 calls bridge_owned, which validates UUIDs and
markers but not the complete bridge port set (src/ownership.cpp:657-665). The
exact port-set requirement is first evaluated inside delete_owned_bridge at
src/ownership.cpp:668-683, after endpoints and namespaces have been deleted.
This violates the complete-set-before-first-deletion contract in M5-007 and
leaves interrupted cleanup unrecoverable under M5-008.

Required remediation: include the complete bridge Port UUID set in the initial
cleanup preflight. With an external adapter port attached, fail before changing
the ledger or any endpoint, namespace, Mirror, Port, Interface, or link. Add a
live regression that proves complete-set preservation and successful cleanup
after the adapter removes its own port.

### M5-R03 — P2: maintained architecture artifacts retain the pre-M5 boundary

docs/GraphX_Architecture.md:19, 29, 41-42, and 79 describe only M3/M4
realization and say namespace veth and semantic-profile flows remain deferred.
M5 now realizes namespace veths, mirrors, router forwarding/policy/routes, and
IPvlan flows. The maintained DOCX contains the same stale statements. Automated
documentation consistency still passes because it does not validate this
achievement wording.

Required remediation: update the maintained source to describe the M5 boundary
while continuing to defer TAP to M6 and declarative capture/fault ownership to
M7, regenerate the DOCX, and visually inspect the rendered pages.

## Acceptance matrix

| ID | Derived requirement | Result | Independent evidence |
|---|---|---|---|
| M5-001 | Fresh portable, quality, sanitizer, projection, documentation, M2/M3/M4, format, and compatibility gates | **PASS** | Fresh debug and quality trees passed 41/41 tests; clang-tidy/cppcheck and LLVM 21 format passed. Fresh LLVM 21 UBSan passed all 41 enabled tests plus coverage; packaging is intentionally disabled only in that profile. A fresh ARM64 Linux tree passed 41/41. |
| M5-002 | Six complete OVS-only migrated plans | **PASS** | All six version-2 configurations validated. Plans contain system OVS, veth, namespace, mirror, route, forwarding, nftables, and semantic-flow operations as applicable and no Docker data-plane network. |
| M5-003 | Frozen version-1 behavior and fingerprints | **PASS** | All five baseline hashes matched exactly, including static-route-policy. |
| M5-004 | Repeated real Lima identity ledger and exact lifecycle | **PASS** | Two invocations of the M5 live regression completed four normal cycles plus two namespace and two first-mirror hard-crash recoveries. Independent mixed-lab inspection matched namespace inode/alias, link ifindices/aliases, container identities, and OVS Bridge/Port/Interface/Mirror UUIDs. |
| M5-005 | Routed packet, named nftables policy, manual route transition, and SPAN packet evidence | **FAIL** | Mixed routed ping, allow/drop counters, and SPAN deltas passed. The declared version-2 manual route cannot be applied through GraphX (M5-R01). |
| M5-006 | Macvlan distinct identity and IPvlan shared identity/steering/isolation | **PASS** | The macvlan wrapper exposed distinct 02:30:...:10/:20/:30 MACs and delivered packets. Mixed IPvlan endpoints and router shared 02:20:00:00:00:01; destination-IP/ARP flows carried traffic, and a real broadcast incremented the OVS drop-flow counter. |
| M5-007 | Namespace, mirror, and endpoint collision/replacement with complete-set preservation | **FAIL** | Namespace and late mirror-peer collisions passed before bridge mutation. Same-marker Mirror UUID replacement and inherited M4 endpoint replacements passed. An expected external port was not caught before sibling deletion (M5-R02). |
| M5-008 | Namespace, endpoint, and mirror interruption/recovery | **FAIL** | Both required create-crash recoveries pass. The partial destroy caused by M5-R02 cannot be resumed with the recorded ledger and required manual cleanup. |
| M5-009 | Management-only Compose and common native-Linux/Lima launch paths | **FAIL** | All Compose models are management-only; mixed, macvlan, IPvlan L2/L3, static-route, and SDR launchers completed normal cycles. The static-route M5 launcher cannot perform its defining apply/clear transition (M5-R01). |
| M5-010 | Accurate external, legacy, M6 TAP, and M7 capture/fault boundaries | **FAIL** | External resources are not adopted and normal adapter-first cleanup works; Docker Desktop is labeled legacy and TAP/M7 deferrals are correct. External-port cleanup safety and the maintained architecture wording are not correct (M5-R02/R03). |

## Passing evidence

- Host debug: 41/41 tests in 23.31 seconds, including package, projections,
  documentation, Lima static, configuration v2, M3, M4, and M5 contracts.
- Host quality: clang-tidy/cppcheck clean and 41/41 tests in 22.45 seconds.
- Host LLVM 21 UBSan: 41/41 enabled tests plus sanitizer coverage in 21.37
  seconds; the packaging-only test was intentionally disabled.
- ARM64 Lima GCC build: 41/41 tests in 19.42 seconds. The VM used vz, rootful
  Docker 29.1.3 with /var/lib/docker, and system Open vSwitch 3.3.9.
- The remediated /var/lib/graphx/runs mode is 0700. Both M5 live invocations,
  retained M3/M4 live regressions, collision probes, and same-marker Mirror UUID
  replacement checks passed.
- Mixed-network delivered routed packets. SPAN receive deltas were 12 and 21
  packets, both nftables accept counters advanced by six, and a broadcast
  incremented the OVS broadcast-drop flow from zero to one.
- Static-route-policy recorded one packet on the named allow rule and one on
  the named deny rule; all three SPAN peers saw traffic.
- All normal M5 launchers tested completed up/status/down. One Docker registry
  metadata timeout succeeded on immediate retry and was environmental.

## Compatibility fingerprints

| Configuration | Observed SHA-256 |
|---|---|
| examples/macvlan/graphx.yaml | 40e09c1cb7ed78b118efeb10dbfe42fc8b06eb10312091f4df7afd9f0920f69d |
| examples/ipvlan-l2/graphx.yaml | da18ff3dfd11a72a834dd6933e161db0a18b92a1ddfc352340208fd274f40f37 |
| examples/ipvlan-l3/graphx.yaml | 598abfa52b274b8d6a3362bc67b141f0852dc590a61b552d99c5960d3b06864b |
| examples/mixed-network/graphx.yaml | ede943d05d0e70cd4bb36970bf94be9ea81787568f313766a807a3c14b6b65fc |
| examples/static-route-policy/graphx.yaml | 33c53b043727d2404f1dcf4a8186dd1ce0b92536b49ead19f20416a166a355db |

## Cleanup

All verifier-created containers, OVS bridges, Mirror rows, links, namespaces,
ownership ledgers/locks, M5 adapter state, images, guest build/log trees, and
host out-of-tree builds were removed. The Lima state root remains empty at mode
0700. The historical M5 verification report remains unchanged.

## Remediation gate

Do not begin M6. Close M5-R01 through M5-R03, then independently repeat the
manual route transition, external-port complete-set refusal and recoverable
cleanup, fresh portable/quality/sanitizer/fingerprint gates, normal route/SDR
launchers, documentation render inspection, and final Lima residue audit.
