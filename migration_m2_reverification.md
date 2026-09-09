# Migration M2 independent re-verification

**Recorded:** 2026-09-09 (America/New_York)  
**Repository:** `~/workspace/graphx-docker`  
**Reviewed HEAD:** `3fd550649f8f3ed476eb7804423f6fcc6cc119fe` with the uncommitted M2 worktree  
**Host:** macOS 26.6.2, arm64  
**Guest:** Lima 2.2.0, instance `graphx`, Ubuntu ARM64 on `vz`

## Verdict

**M2 passes independent re-verification.**

All six findings in the original `migration_m2_verification.md` are closed.
The authoritative loader, schema, migration command, inspection surface,
compatibility behavior, tests, documentation, and Lima regression evidence
satisfy M2-001 through M2-010. No M2 product finding remains.

This verification did not change product code, commit, publish, deploy, or
advance to M3. The original failed verification report remains unchanged as a
historical record.

Packet realization of the five semantic profiles is not M2 evidence and remains
not yet applicable until the later OVS, veth, and TAP realization milestones.

## Acceptance matrix

| ID | Requirement | Independent evidence | Status |
|---|---|---|---|
| M2-001 | Explicit integer v1/v2 boundary | Fresh CLI checks rejected versions 0, 3, -1, 1.0, boolean, null, and quoted 1/2. Draft 2020-12 and the C++ loader agree. Version-aware parsing rejected v1-only keys in v2. | Implemented |
| M2-002 | Five exact semantic profiles | Typed definitions, documentation, and `graphx inspect` agree on all nine fields for `ethernet`, `macvlan`, `ipvlan-l2`, `ipvlan-l3`, and `ipvlan-l3s`. Exact-value tests passed on host and guest. | Implemented |
| M2-003 | Five strict attachment kinds | Independent mutations exercised all five kinds. Unknown and mismatched network, address, owner, interface, peer, switch, mirror ID, output port, resolved interface, runtime, deployment, subnet, and MAC cases were rejected. | Implemented |
| M2-004 | Literal deterministic migration | Seven repository inputs produced byte-identical repeated output. `GRAPHX_OVERRIDES=version=2` did not alter any result. An explicit ipvlan l3s input mapped to `ipvlan-l3s`. | Implemented |
| M2-005 | Safe output | Standard output left the source byte-identical. Explicit output was mode 0600. Existing, source, dangling-symlink, duplicate-output, and unknown-option targets were rejected. Exclusive/no-follow creation and partial-write unlink are present in the reviewed CLI path. | Implemented |
| M2-006 | Version-1 compatibility | Fresh portable suite passed, projections are current, and all five frozen dry-run SHA-256 values match M0 exactly. | Implemented |
| M2-007 | Fail-closed v2 realization | Create, destroy, status, route apply/clear, and fault apply/clear all stopped at the M3 boundary under dry-run and emitted no legacy Docker-network command. | Implemented |
| M2-008 | Schema, inspection, and projection surfaces | Schema separates v1/v2 network definitions, exposes the five profiles and attachments, accepts only the system datapath for v2, and agrees with CLI validation and inspection. | Implemented |
| M2-009 | Documentation | Configuration, migration, upgrade/rollback, security, support, architecture, implementation, and verifier documents consistently describe literal migration, the v1 compatibility window, and the M2/M3 boundary. | Implemented |
| M2-010 | Required gates | Fresh 38-test build, quick, quality, sanitizer, compatibility, projection, Lima, and guest schema checks passed. | Implemented |

## Original-finding closure

| Original finding | Independent result |
|---|---|
| M2-V-001 contradictory OVS attachment references | Closed. Every supplied switch resolves; namespace attachments exactly match one owner-router interface; mirrors match switch, ID, configured output port, and resolved interface. |
| M2-V-002 ambient override affected migration | Closed. All seven migrations were identical with a clean environment and hostile `GRAPHX_OVERRIDES=version=2`. |
| M2-V-003 loader/schema version disagreement | Closed. Both reject quoted versions 1 and 2; broader non-integer and out-of-range CLI cases also reject. |
| M2-V-004 quality gate failure | Closed. clang-format, clang-tidy, and cppcheck passed all configured production, application, and test targets. |
| M2-V-005 weak semantic tests | Closed. Tests and independent inspection compare all nine exact fields for all five profiles. |
| M2-V-006 stale verifier contract | Closed. `prompt/implement.md` and `prompt/verifier.md` are M2-specific and contain the remediated adversarial requirements. |

## Independent evidence

### Portable macOS

- Fresh out-of-tree AppleClang C++20 build at
  `/tmp/graphx-m2-reverify.PGq7Cq/build`: **38/38 tests passed** in 22.69
  seconds.
- `scripts/verify.sh quick`: **38/38 passed** in 30 seconds. Log:
  `outputs/verification/20260909T162242Z-quick.log`.
- `scripts/verify.sh quality`: **passed** in 14 seconds, including 47-file
  formatting, clang-tidy, and cppcheck. Log:
  `outputs/verification/20260909T162047Z-quality.log`.
- `scripts/verify.sh sanitizers`: **38 enabled tests passed** under the macOS
  LLVM 21 UBSan profile in 30 seconds. The package test is intentionally
  disabled in that profile. Log:
  `outputs/verification/20260909T162204Z-sanitizers.log`.
- `graphx project graphx.yaml --check --output-dir config`: projections current.
- `git diff --check`: passed before this report was written.

The first sanitizer attempt had one UDP-unicast example failure after the
publisher sent all five packets. The isolated test immediately passed with all
five packets received, and the complete sanitizer profile then passed. No
sanitizer diagnostic was emitted. This is recorded as a non-blocking timing
observation, not hidden as a successful first attempt. The failed log is
`outputs/verification/20260909T162109Z-sanitizers.log`.

macOS 26 uses the repository's LLVM 21 UBSan profile because its Homebrew LLVM
AddressSanitizer runtime hangs during process initialization. The Linux profile
retains combined AddressSanitizer and UndefinedBehaviorSanitizer acceptance.

### Migration and adversarial behavior

- Root, macvlan, ipvlan-l2, ipvlan-l3, mixed-network,
  static-route-policy, and qemu-node inputs were migrated twice, compared byte
  for byte, validated by the C++ loader, inspected, and repeated with hostile
  environment overrides.
- A derived ipvlan `l3s` input migrated and inspected as `ipvlan-l3s`.
- Independent text mutations rejected every namespace router-field mismatch,
  unknown attachment switches, every mirror relationship mismatch, invalid
  container/QEMU/external ownership, missing required fields, unknown networks,
  out-of-subnet addresses, malformed MACs, v1-only fields in v2, and a non-system
  OVS datapath.
- Unsupported macvlan mode, ambiguous bridge fields, an attachment-ID collision,
  and an input over 1 MiB failed with bounded diagnostics.
- Output safety and all seven v2 realization entry points were independently
  exercised.

### Version-1 compatibility fingerprints

| Configuration | Observed SHA-256 | M0 match |
|---|---|---|
| `examples/macvlan/graphx.yaml` | `40e09c1cb7ed78b118efeb10dbfe42fc8b06eb10312091f4df7afd9f0920f69d` | Yes |
| `examples/ipvlan-l2/graphx.yaml` | `da18ff3dfd11a72a834dd6933e161db0a18b92a1ddfc352340208fd274f40f37` | Yes |
| `examples/ipvlan-l3/graphx.yaml` | `598abfa52b274b8d6a3362bc67b141f0852dc590a61b552d99c5960d3b06864b` | Yes |
| `examples/mixed-network/graphx.yaml` | `ede943d05d0e70cd4bb36970bf94be9ea81787568f313766a807a3c14b6b65fc` | Yes |
| `examples/static-route-policy/graphx.yaml` | `33c53b043727d2404f1dcf4a8186dd1ce0b92536b49ead19f20416a166a355db` | Yes |

### Lima ARM64 Linux

- `infrastructure/lima/verify.sh`: **passed** system OVS, namespace/veth/TAP,
  packet exchange/capture, nftables, netem, rootful Docker, projections,
  cleanup, and all 38 GraphX tests.
- Passing retained evidence:
  `/var/lib/graphx/m1/evidence/20260909T162654Z-5ec8a771`.
- Ubuntu Draft 2020-12 validation accepted **15 documents**: clean and hostile
  outputs for all seven representative migrations plus ipvlan-l3s.
- Guest schema and C++ loader both rejected quoted versions 1 and 2.
- Final Lima instance state: **Stopped**, matching its initial state.

The first Lima attempt failed its exact listener snapshot because
`systemd-timesyncd` briefly opened an unrelated UDP socket. Inspection showed
the process was a guest system service, the socket closed without intervention,
and no `gx-m1-*` namespace, link, OVS bridge, Docker container, or nftables
object remained. The complete rerun then passed. The failed evidence is retained
at `/var/lib/graphx/m1/evidence/20260909T162532Z-b50ade9e`.

## Remaining boundary

M2 validates intent; it does not realize semantic packet behavior. Persistent
OVS ownership and lifecycle begin in M3, managed container veth attachment in
M4, migrated runtime examples in M5, QEMU TAP attachment in M6, and generalized
capture/fault lifecycle later. Those packet and realization claims remain
**Not Yet Applicable** to this verdict.
