# Migration M4 independent re-verification

**Re-verified:** 2026-09-09 (America/New_York)  
**Repository:** `~/workspace/graphx-docker`  
**Branch/base:** `main` at `3534b1ca9a29bd8a059292b78e448537eeabc89f`  
**Product:** GraphX 1.1.0  
**Prior verification SHA-256:** `0954cc10888a542b0b38941a16f989ff5dca2aab6cce6524684f0e1a6730f0d0`  
**Verification state:** implementation files left unchanged

## Verdict

**CHANGES REQUIRED — the functional M4 remediation passes, but M4 is not yet
accepted and must not advance to M5.**

The two prior findings are closed. Independent ARM64 Lima evidence proves that
same-name copied-marker OVS Port and Interface replacements and a same-name
copied-alias host-link replacement all fail closed before cleanup mutation. The
replacement, bridge, host link where applicable, and ownership ledger remain.
Normal cleanup, rollback, hard-crash recovery, immediate retry, container
identity/restart behavior, collisions, endpoint configuration, and retained M3
behavior pass. The architecture source and all 24 rendered DOCX pages accurately
describe M4 and its deferred boundaries.

Acceptance remains blocked because the required pinned LLVM 21 formatting gate
fails with 95 diagnostics in three M4 source files. Fresh debug, static-analysis,
sanitizer, macOS, and Linux builds otherwise pass.

## Finding

### [P1] M4-001: pinned formatting gate fails

`CLANG_FORMAT=/opt/homebrew/opt/llvm@21/bin/clang-format
scripts/check-format.sh` returns 1 with 95 `code should be clang-formatted`
diagnostics across:

- `src/config.cpp`, beginning at line 1064;
- `src/migration.cpp`, beginning at line 72; and
- `src/ownership.cpp`, beginning at line 204, including the remediated bridge
  and endpoint ownership code around lines 548 through 635.

The C++20 clang-tidy/cppcheck build succeeds, so this is specifically the
repository's separate formatting gate. M4's implementation contract requires
the quality gates to pass; the phase therefore cannot be accepted until the
three files are formatted with the pinned tool and all affected regression and
live gates are repeated.

## Acceptance matrix

| ID | Derived requirement | Result | Independent evidence |
|---|---|---|---|
| M4-001 | Fresh portable, quality, sanitizer, migration, M3, projection, documentation, formatting, and fingerprint gates | **Partial — blocker** | Fresh debug, quality, sanitizer, macOS, and Linux test profiles pass. Clang-tidy/cppcheck pass. The pinned clang-format 21 gate fails in three M4 source files. |
| M4-002 | Dry-run declares Compose identity, veth, OVS port, address, and MTU without later-phase or legacy mutation | **Implemented** | The fresh portable M4 contract test passes and rejects Docker-network/MACVLAN/IPVLAN, namespace, TAP, mirror, fault, capture, and process mutation. |
| M4-003 | Select exactly one verified Compose service and persist full container/namespace identity | **Implemented** | Independent Lima inspection matched labels, image, 64-character container ID, PID, and `/proc/<pid>/ns/net` inode. A duplicate matching service was rejected before mutation. |
| M4-004 | Own and verify veth/OVS identities and declared endpoint settings | **Implemented** | Ledger ifindices and OVS UUIDs matched live objects; host/peer aliases and intrinsic OVS markers matched. MAC `02:79:00:00:00:02`, address `10.79.0.2/24`, MTU 1400, and route `10.80.0.0/24 via 10.79.0.1` were observed. |
| M4-005 | Two clean cycles with management separate from OVS data plane | **Implemented** | Checked-in Lima regression completed repeated cycles. Docker exec management remained available while the only OVS data port was the extra GraphX host veth. |
| M4-006 | Restart/replacement detection and explicit safe reattachment | **Implemented** | Replacement changed container identity; status failed with the documented replacement state, and explicit destroy/create restored healthy attachment without adoption. |
| M4-007 | Collision and replacement refusal with complete-set preservation | **Implemented** | Independent unowned host, OVS Port/Interface, and target-interface collisions failed before desired bridge creation. Checked-in copied-marker Port/Interface and copied-alias/changed-ifindex replacements returned nonzero and preserved the complete set. |
| M4-008 | Failure rollback, hard-crash recovery, and immediate retry | **Implemented** | Injected failure removed endpoint, bridge, and ledger. Exit-99 interruption left recoverable state; recover removed owned resources and an immediate create/destroy retry passed. |
| M4-009 | Deterministic M2 migration without invented addresses | **Implemented** | Two migrations matched SHA-256 `a782b28ff7a97f95060d25dc3e45ce35de51bc1ea724dac24db26579a7574e6b`. The result validated, while live Linux realization refused the absent address before publishing state. |
| M4-010 | Accurate scope documentation and deferred-boundary statements | **Implemented** | Markdown identifies M3 bridges and M4 managed-container veth endpoints while deferring namespace veth, TAP, profile flows, mirrors, general routing/policy, faults, and capture realization. The 24-page DOCX render is clean. |

## Fresh portable and quality evidence

The verifier used a new out-of-tree root at
`/var/tmp/graphx-m4-reverify.WSN18o`:

- Debug AppleClang build: 40 of 40 tests passed, including package, M2
  migration, M3 ownership, M4 container-veth, projections, documentation, and
  Lima static checks.
- Quality AppleClang C++20 build: clang-tidy and cppcheck completed without a
  diagnostic; 40 of 40 tests passed.
- Sanitizer AppleClang C++23 build: AddressSanitizer and UBSan instrumentation
  coverage passed; all 40 enabled tests passed with unsupported macOS leak
  detection disabled. The package test is intentionally disabled in this
  profile.
- A separate fresh ARM64 Linux GCC build passed all 40 tests.
- `git diff --check`, JSON Schema parsing, projection drift, and documentation
  consistency passed.
- The first sanitizer invocation incorrectly forced unsupported macOS leak
  detection and caused pre-test aborts. The unchanged binaries passed when
  rerun with `detect_leaks=0`; those environmental aborts are not product
  failures.

All five frozen M0 version-1 plan fingerprints remain exact:

| Configuration | Observed SHA-256 |
|---|---|
| `examples/macvlan/graphx.yaml` | `40e09c1cb7ed78b118efeb10dbfe42fc8b06eb10312091f4df7afd9f0920f69d` |
| `examples/ipvlan-l2/graphx.yaml` | `da18ff3dfd11a72a834dd6933e161db0a18b92a1ddfc352340208fd274f40f37` |
| `examples/ipvlan-l3/graphx.yaml` | `598abfa52b274b8d6a3362bc67b141f0852dc590a61b552d99c5960d3b06864b` |
| `examples/mixed-network/graphx.yaml` | `ede943d05d0e70cd4bb36970bf94be9ea81787568f313766a807a3c14b6b65fc` |
| `examples/static-route-policy/graphx.yaml` | `33c53b043727d2404f1dcf4a8186dd1ce0b92536b49ead19f20416a166a355db` |

## ARM64 Lima and adversarial evidence

The `graphx` Lima VM reported `aarch64`, `vz`, rootful Docker, and system Open
vSwitch 3.3.9. A fresh guest build and disposable Debian image were used.

The checked-in M4 live regression passed normal cycles, restart/reattachment,
copied-marker Port and Interface replacement, copied-alias host-ifindex
replacement, ordinary rollback, hard-crash recovery, immediate retry, and exact
cleanup. The retained M3 live ownership regression passed.

A separate verifier-only probe passed exact service selection, persisted and
observed container/namespace identity, both veth ifindices and aliases, OVS
Port/Interface and implicit bridge-Port UUIDs, intrinsic markers, MAC/address/
MTU/route configuration, Docker management access, and OVS-only data attachment.
It also passed duplicate-service, unowned host, unowned OVS, target-namespace,
and missing-address refusals before desired-state publication.

Final inspection found no targeted containers, OVS bridges, host links, guest
probe script, guest build tree, or disposable image.

## Documentation evidence

Documentation consistency passes. The corrected achievement table identifies
M4 managed-container veth endpoints. Namespace/TAP endpoints and profile flows
remain deferred, as do mirrors, general routing/policy, faults, and capture
realization. The maintained DOCX SHA-256 is
`2543b42e0e19c07877448709156dfbac9ebd5c64db0b2bb8b55c80ee5c775cd7`.
The packaged renderer produced 24 pages, all inspected at full-page resolution
with no clipping, overlap, broken tables, missing glyphs, or misplaced headers
and footers.

## Required remediation

Do not advance to M5. Format `src/config.cpp`, `src/migration.cpp`, and
`src/ownership.cpp` with the repository's pinned clang-format 21 tool. Then
independently repeat the format, fresh debug/quality/sanitizer, M0 fingerprint,
M2/M3/M4 portable, copied-marker/copy-alias replacement, rollback/recovery,
DOCX, and final Lima cleanup gates.
