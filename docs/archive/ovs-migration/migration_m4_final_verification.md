# Migration M4 final independent verification

**Verified:** 2026-09-09 (America/New_York)  
**Repository:** `~/workspace/graphx-docker`  
**Branch/base:** `main` at `3534b1ca9a29bd8a059292b78e448537eeabc89f`  
**Product:** GraphX 1.1.0  
**Prior re-verification SHA-256:** `4b00edb6d254548704dc194867eab918b6e02d0d1331ce7289b93a2e1db86467`  
**Formatting remediation SHA-256:** `062ef69fe78dd51214e31606cc0cd9c966eeef2f28cbd03b947930775943c1c1`  
**Verification state:** product implementation left unchanged; this report and status metadata are the only verification additions

## Verdict

**ACCEPTED — M4 satisfies its managed-container veth boundary. M5 may begin as
a separate work package.**

The remaining LLVM 21 formatting finding is closed. Fresh macOS and ARM64 Linux
builds, portable and privileged regressions, frozen compatibility fingerprints,
deterministic migration, independent identity/collision probes, documentation,
and exact cleanup all pass. No M4 acceptance finding remains.

## Acceptance matrix

| ID | Derived requirement | Result | Independent evidence |
|---|---|---|---|
| M4-001 | Fresh portable, quality, sanitizer, migration, M3, projection, documentation, formatting, and fingerprint gates | **Implemented** | Pinned clang-format 21 passes all 49 C++ files. Fresh development and quality profiles each pass 40/40; clang-tidy/cppcheck pass; LLVM 21 UBSan passes all 40 enabled tests plus coverage. A fresh Linux GCC profile passes 40/40. |
| M4-002 | Dry-run declares Compose identity, veth, OVS port, address, and MTU without later-phase or legacy mutation | **Implemented** | The fresh M4 portable contract passes its required markers and excludes Docker-network, legacy driver, namespace, TAP, mirror, fault, capture, and process mutation. |
| M4-003 | Select exactly one verified Compose service and persist full container/namespace identity | **Implemented** | The verifier matched project/service labels, image, running state, full 64-character container ID, PID, and `/proc/<pid>/ns/net` inode. A duplicate matching service was rejected before state publication or network mutation. |
| M4-004 | Own and verify veth/OVS identities and declared endpoint settings | **Implemented** | Ledger ifindices, namespace identity, OVS Port/Interface UUIDs, implicit bridge-Port UUID, aliases, and intrinsic markers matched live objects. MAC `02:79:00:00:00:02`, address `10.79.0.2/24`, MTU 1400, and route `10.80.0.0/24 via 10.79.0.1` matched the declaration. |
| M4-005 | Two clean cycles with management separate from the OVS data plane | **Implemented** | The privileged regression completed repeated create/status/destroy cycles. Docker management remained reachable while the OVS bridge contained only the extra GraphX host veth data port. |
| M4-006 | Restart/replacement detection and explicit safe reattachment | **Implemented** | Container replacement changed full ID and namespace inode. Status failed closed with `missing-replaced-or-restarted`; explicit destroy/create attached the replacement without adoption. |
| M4-007 | Collision and replacement refusal with complete-set preservation | **Implemented** | Unowned host, OVS Port/Interface, and target-interface collisions failed before desired-state publication. Same-name copied-marker Port/Interface replacements and copied-alias/changed-ifindex host replacements returned nonzero and preserved replacement, bridge, host link, and ledger. |
| M4-008 | Failure rollback, hard-crash recovery, and immediate retry | **Implemented** | Injected failure removed endpoint, bridge, and ledger. Exit-99 interruption left recoverable owned state; recover removed it and immediate create/destroy retry passed. |
| M4-009 | Deterministic M2 migration without invented addresses | **Implemented** | Repeated root migration matches SHA-256 `a782b28ff7a97f95060d25dc3e45ce35de51bc1ea724dac24db26579a7574e6b` and validates. Missing-address realization fails before state or network mutation. |
| M4-010 | Accurate scope documentation and deferred-boundary statements | **Implemented** | Documentation identifies M3 bridges and M4 managed-container veth endpoints while deferring namespace/TAP endpoints, profile flows, mirrors, general routing/policy, faults, and capture realization. The maintained 24-page DOCX renders cleanly. |

## Fresh portable and quality evidence

The verifier used the new out-of-tree root
`/var/tmp/graphx-m4-final-verify.81YNwy`:

- Debug AppleClang build: 40 of 40 tests passed in 10.11 seconds.
- Quality AppleClang C++20 build: clang-tidy and cppcheck completed cleanly;
  40 of 40 tests passed in 10.12 seconds.
- LLVM 21 C++23 UBSan build: all 40 enabled tests plus sanitizer coverage
  passed in 6.83 seconds; the package test is intentionally disabled in this
  profile.
- Pinned LLVM 21 format check: all 49 repository C++ files passed.
- JSON Schema parsing, `git diff --check`, projection behavior, documentation
  consistency, M2 migration, M3 ownership, and M4 portable checks passed.

All five frozen M0 version-1 dry-run SHA-256 values remain exact:

| Configuration | Observed SHA-256 |
|---|---|
| `examples/macvlan/graphx.yaml` | `40e09c1cb7ed78b118efeb10dbfe42fc8b06eb10312091f4df7afd9f0920f69d` |
| `examples/ipvlan-l2/graphx.yaml` | `da18ff3dfd11a72a834dd6933e161db0a18b92a1ddfc352340208fd274f40f37` |
| `examples/ipvlan-l3/graphx.yaml` | `598abfa52b274b8d6a3362bc67b141f0852dc590a61b552d99c5960d3b06864b` |
| `examples/mixed-network/graphx.yaml` | `ede943d05d0e70cd4bb36970bf94be9ea81787568f313766a807a3c14b6b65fc` |
| `examples/static-route-policy/graphx.yaml` | `33c53b043727d2404f1dcf4a8186dd1ce0b92536b49ead19f20416a166a355db` |

## ARM64 Lima evidence

The `graphx` VM reported `aarch64`, Apple `vz`, rootful Docker with VM-local
`/var/lib/docker`, and system Open vSwitch 3.3.9. A fresh native guest build
passed all 40 tests in 9.39 seconds.

The checked-in M4 live regression passed normal cycles, container replacement
and reattachment, copied-marker Port and Interface replacement, copied-alias
host-ifindex replacement, ordinary rollback, hard-crash recovery, immediate
retry, and exact cleanup. The retained M3 live ownership regression passed.

A verifier-only probe independently matched Docker and namespace identity,
veth ifindices and aliases, OVS UUIDs and markers, endpoint MAC/address/MTU/
route state, Docker management access, and the OVS-only data port. It also
proved duplicate-service, unowned host, unowned OVS, target-interface, and
missing-address refusals before publication. An initial probe used an OVS
`internal` interface, which correctly created a host link and triggered the
earlier host-collision guard; the corrected OVS-only system-row probe passed.
This was a verifier-fixture correction, not a product change or failure.

Final inspection found no targeted containers, images, OVS bridges, host links,
guest probe, guest build tree, or ownership state. Lima remains running in its
initial configuration.

## Documentation evidence

Documentation consistency passes. The architecture source distinguishes M4
managed-container veth endpoints from deferred namespace/TAP endpoints and
profile flows. Mirrors, general routing/policy, faults, and capture realization
remain explicitly deferred.

The maintained DOCX SHA-256 is
`2543b42e0e19c07877448709156dfbac9ebd5c64db0b2bb8b55c80ee5c775cd7`.
The packaged renderer produced 24 pages. Every page was visually inspected at
full-page resolution with no clipping, overlap, broken tables, missing glyphs,
or misplaced headers and footers.

## Closure

The historical `CHANGES REQUIRED` reports remain unchanged. Their copied-marker
replacement, architecture wording, and formatting findings are closed by the
remediation records and this final independent verification. M4 is accepted;
namespace veth, QEMU TAP, semantic-profile flows, mirrors, general router policy,
faults, and capture realization remain later migration work.
