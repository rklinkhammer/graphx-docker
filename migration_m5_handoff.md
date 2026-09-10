# Migration M5 implementation handoff

**Status:** Remediated after independent re-verification; final independent acceptance required  
**Date:** 2026-09-10

## Delivered boundary

M5 extends the persistent ownership ledger to Linux router namespaces,
namespace veth endpoints, OVS SPAN veth/Port/Interface/Mirror identities,
forwarding, declared router routes, and nftables forward policy. IPvlan semantic
profiles install destination-IP and ARP-target steering through OVS and suppress
non-ARP broadcast/multicast. Managed containers still use the M4 identity-bound
veth pipeline and Docker retains management connectivity only.

The mixed-network, macvlan, ipvlan-l2, ipvlan-l3, static-route-policy, and
external-SDR examples now have checked-in `graphx-ovs.yaml` variants. The four
managed-container pipelines also have management-only `compose.ovs.yaml` files
and common `scripts/ovs-{up,status,down}.sh` wrappers. Frozen version-1 files
remain unchanged for compatibility. The Docker Desktop userspace-OVS path is
now explicitly labeled a legacy simulation.

`external` attachments remain an intentional ownership boundary: M5 plans them
but does not adopt or delete the SDR device or diagnostic endpoint namespaces.
The static-route and SDR examples now provide `ovs-lab.sh` adapters that create
only those lab-owned external boundaries and select the M5 configuration. Their
version-1 launchers remain available as compatibility workflows. TAP remains M6.
Declarative capture, rotation, and netem/fault ownership remain M7; existing lab
helpers continue to provide those operations during the window.

## Independent-verification remediation

- Lima provisioning now creates `/var/lib/graphx/runs` with the mode 0700
  required by the ownership ledger, with a portable regression for that exact
  contract.
- Crash recovery now captures the actual mirror peer ifindex when a hard crash
  lands between the atomic mirror mutation and ledger publication. The Linux
  live regression exercises both namespace publication and the exact first
  mirror crash boundary.
- M5 dry-run output now enumerates namespace creation, attachment and router
  routes, forwarding, nftables policy, and semantic OVS flow programming.
- Static-route-policy and external-SDR have M5 launchers. The SDR M5 Compose
  file is management-only and contains no Docker data-plane network.

## Independent re-verification remediation

- Version-2 `infra route apply|clear` now accepts only a validated Linux
  namespace router and an exact route destination already declared in the
  configuration. The static-route M5 launcher exposes `apply-route` and
  `clear-route` actions for its declared `10.64.30.10/32` transition.
- Destroy now compares every live Bridge Port UUID with the internal port and
  all still-present ledger-owned endpoint ports during the initial preflight.
  An adapter-owned or otherwise unexpected port therefore refuses the entire
  cleanup before the ledger, mirror, namespace, endpoint, link, Port, or
  Interface changes.
- The privileged M5 lifecycle regression attaches an unrecorded OVS port,
  proves destroy preserves the complete managed resource set and byte-exact
  ledger, removes the external port, and then proves ordinary cleanup succeeds.
- The architecture Markdown and regenerated DOCX now describe the implemented
  M5 namespace, mirror, router, policy, and semantic-flow boundary. Automated
  consistency checks guard that wording while retaining TAP for M6 and
  declarative capture/fault ownership for M7.

Second remediation validation completed on 2026-09-10:

- macOS development and quality profiles passed all 41 tests; the sanitizer
  profile passed all 41 enabled tests plus its coverage gate, with only the
  intentionally disabled packaging test omitted. LLVM 21 formatting,
  clang-tidy, and cppcheck passed.
- A fresh ARM64 Lima GCC build passed all 41 tests. The privileged M5 lifecycle
  test then passed both normal cycles, both crash recoveries, and the new
  unexpected-port complete-set refusal/recovery check.
- The actual static-route M5 launcher proved the route initially absent,
  applied the exact declared next hop/device, delivered a receiver-confirmed
  UDP datagram, cleared the route, and proved it absent again.
- The regenerated 24-page DOCX was rendered and every page was visually
  inspected without clipping, overlap, broken tables, or missing content.
- The final Lima audit found no test bridge, namespace, ownership ledger,
  launcher state, or remediation build tree.

Remediation validation completed on 2026-09-09:

- The macOS development suite passed all 41 tests; the sanitizer suite passed
  all 41 enabled tests, with only the intentionally disabled packaging test not
  run; LLVM 21 formatting, clang-tidy, and cppcheck passed.
- All six migrated configurations validated and emitted complete OVS-only
  plans, while the four frozen version-1 fingerprints remained exact.
- In the ARM64 Lima VM, the M5 privileged regression passed both injected hard
  crashes (namespace publication and first mirror mutation) and two normal
  lifecycle cycles.
- The actual mixed-network wrapper and both external-boundary launchers passed
  `up`, `status`, and `down`. The SDR boundary also reached its managed
  processor over the GraphX-owned OVS data plane.
- The final Lima audit found no running test containers, OVS bridges, network
  namespaces, ownership ledgers, or remediation images/build trees.

## Evidence completed

- Clang formatting passes for the modified C++ interface and implementation.
- The development build passes all 41 portable tests, including the new M5
  migration contract; M3 and M4 regressions remain green.
- The sanitizer build passes all 41 enabled tests (the packaging-only test is
  intentionally disabled under sanitizers).
- The quality build passes clang-tidy/cppcheck compilation.
- All six M5 configurations validate and produce OVS-only dry-run plans.
- The four frozen M0 version-1 dry-run fingerprints remain exact.
- In the ARM64 GraphX Lima VM, the privileged M5 live test passed two complete
  cycles. It proved a packet routed between two container namespaces through an
  owned Linux router namespace, inspected semantic OVS flows and mirror ledger
  identities, then removed all bridges, veths, mirrors, namespace state, and
  test containers.

## Independent verification focus

Repeat the live test from a fresh build and broaden collision/replacement and
hard-interruption coverage around namespace and mirror mutations. Inspect
nftables counters and packets for each semantic profile, run the actual managed
example wrappers in both Lima and native Linux, and confirm the external
attachment boundary is documented and preserved. Do not advance to M6 until
that evidence is accepted.
