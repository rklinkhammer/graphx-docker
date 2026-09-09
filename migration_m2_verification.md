# Migration M2 independent verification

**Recorded:** 2026-09-08 (America/New_York)  
**Repository:** `~/workspace/graphx-docker`  
**Reviewed HEAD:** `3fd550649f8f3ed476eb7804423f6fcc6cc119fe` with the uncommitted M2 worktree  
**Host:** macOS 26.6.2, arm64  
**Guest:** Lima `graphx`, Ubuntu ARM64 on `vz`  

## Verdict

**M2 does not yet pass independent verification.**

The explicit version boundary, five semantic-profile definitions, five
attachment kinds, deterministic migration for a controlled environment,
version-1 compatibility, safe output behavior, projections, and fail-closed M3
boundary are implemented. A clean macOS build passed all 38 tests, the macOS
UBSan profile passed, the five frozen M0 planner fingerprints are unchanged,
and a real Lima guest passed its OVS/veth/TAP regression cycle and validated
all representative migrations with Draft 2020-12 JSON Schema.

The gate remains failed because strict v2 validation accepts contradictory OVS
attachment references, migration behavior is affected by ambient
`GRAPHX_OVERRIDES`, the C++ loader and JSON Schema disagree on the type of
`version`, and the repository quality gate fails on new M2 test code. The
active independent-verifier prompt also still defines M1 rather than M2.

No product implementation was changed during this verification. This report is
the only added repository file.

## Findings

### M2-V-001 — High — attachment references can contradict the OVS model

`namespace_veth` parsing requires a non-empty `switch`, but semantic validation
checks only that its owner is a Linux namespace router. It does not require the
named switch to exist or require the attachment to agree with the corresponding
router interface. A migrated `mixed-network` v2 file remained valid after
changing the attachment switch from `br-gx-mac` to `nonexistent`.

Mirror validation similarly accepts any interface on the named switch instead
of the interface selected by `switch.mirror.output_port`. The same migrated file
remained valid after changing `mirror-mac.interface` from `cap-mac-ovs` to
`mv-ovs`, even though the switch still selected the `capture` port.

Relevant code: `src/config.cpp:1088-1101` and `src/config.cpp:1744-1773`.

Impact: M3 would receive two accepted but contradictory sources of realization
intent. Choosing either one silently would violate the explicit, reviewable OVS
model and could attach a veth or observation endpoint to the wrong bridge/port.

Required remediation:

1. For every attachment with `switch`, require that switch to exist.
2. Require each `namespace_veth` to match exactly one interface on its owner
   router: network, address, device/interface, peer, and switch.
3. Require a `mirror` attachment to match the switch mirror ID and the interface
   resolved through its configured `output_port`.
4. Add negative tests for unknown switches and every router/mirror mismatch.

### M2-V-002 — Medium — migration consumes ambient runtime overrides

`migrate_config_v1_to_v2()` calls the general `load_config()` API. That API
always applies `GRAPHX_OVERRIDES`, after which migration separately reloads the
literal YAML file for emission. A valid v1 `graphx.yaml` fails migration when
the caller happens to have `GRAPHX_OVERRIDES=version=2` in the environment.
The focused CTest explicitly clears this variable, so the regression is hidden
from the automated migration test.

Relevant code: `src/migration.cpp:79-89`, `src/config.cpp:1839-1864`, and
`CMakeLists.txt:274-277`.

Impact: the command does not always validate the source file with literal v1
semantics as documented, and its success/failure is not determined solely by
the stated input. Other overrides may validate a normalized model that differs
from the raw document being emitted.

Required remediation: add a literal-source load mode that does not consume
environment or CLI overrides, use it for migration, document that policy, and
add a test that runs migration with hostile `GRAPHX_OVERRIDES` values.

### M2-V-003 — Medium — schema and authoritative loader disagree on `version`

The authoritative C++ loader accepts `version: "1"` as version 1 because the
generic unsigned conversion accepts a quoted scalar. The JSON Schema correctly
rejects the same value because it is not the integer 1 or 2.

Impact: a document can pass `graphx validate` and fail the published schema,
which weakens the claimed strict version boundary and makes validation results
tool-dependent.

Required remediation: use a strict unsigned scalar conversion for `version`
(and add quoted-version tests to both validation paths), or deliberately relax
and document both contracts together. Strict integer typing is preferred.

### M2-V-004 — Medium — the quality gate fails on new M2 test code

`scripts/verify.sh quality` passes formatting, then fails static analysis with
`uninitMemberVarNoCtor` for the anonymous profile-case structure at
`tests/test_config.cpp:114-118`.

Impact: the repository's required static-analysis gate is red despite the
runtime suite passing.

Required remediation: initialize the enum member or use a fully initialized
named case type, then rerun the complete quality profile.

### M2-V-005 — Low — semantic contract tests are too weak

The profile test asserts that most behavior fields are merely non-empty; it
checks exact values only for routing and management. A change from `endpoint`
to another non-empty MAC/ARP value would pass, even though these strings are the
frozen acceptance contract for later packet tests.

Relevant code: `tests/test_config.cpp:114-133`.

Required remediation: table-drive all nine exact fields for all five profiles,
and verify the same values in `graphx inspect` output.

### M2-V-006 — Low — the active verifier contract is stale

`prompt/verifier.md:1-8` still instructs an independent M1 verification and an
M1 report. M2 therefore began without the phase-specific frozen verification
work package required by the common evidence rules.

Required remediation: replace or version the verifier prompt with M2-specific
requirements before remediation is re-verified. Preserve the M1 verifier as a
historical prompt if it remains useful.

## Acceptance matrix

| ID | Requirement | Independent evidence | Status |
|---|---|---|---|
| M2-001 | Explicit v1/v2 compatibility boundary | Version-aware keys and no silent `driver` reinterpretation work; quoted versions expose a schema/loader type mismatch. | Partial |
| M2-002 | Five fixed semantic profiles | All five definitions and nine behavior fields exist; `ipvlan-l3s` migration was independently exercised. Exact-value regression coverage is incomplete. | Partial |
| M2-003 | Five typed, strict attachments | Kinds, owners, subnet membership, and basic mirror ownership exist. Unknown/mismatched switch and mirror references are accepted. | Partial |
| M2-004 | Deterministic, reviewable v1-to-v2 migration | Seven repository inputs were byte-identical across repeated runs and validated; `ipvlan-l3s` and ambiguous-input rejection were also checked. Ambient overrides change behavior. | Partial |
| M2-005 | Non-destructive, safe output | Standard output is default; exclusive 0600 creation, source/existing/symlink refusal, duplicate-option refusal, and partial-write cleanup are implemented and tested. | Implemented |
| M2-006 | Preserve v1 behavior | Clean suite passed; projections are current; all five M0 dry-run hashes match exactly. | Implemented |
| M2-007 | No v2 path through the legacy Docker planner | Create, destroy, status, route apply/clear, and fault apply/clear all failed with the M3 diagnostic and emitted no Docker-network command. | Implemented |
| M2-008 | Versioned schema, inspection, and projection | Separate v1/v2 surfaces and v2 inspection/projection work. Loader/schema disagreement remains. | Partial |
| M2-009 | Operator and architecture documentation | Main M2 boundary is documented, but migration's override behavior is neither excluded nor documented and the verifier prompt is stale. | Partial |
| M2-010 | Portable and Linux regression gates | macOS clean runtime tests, UBSan, and Lima passed; the quality/static-analysis gate failed. | Partial |

## Independent evidence

### Portable macOS

- Fresh out-of-tree AppleClang 21 C++20 build: **38/38 tests passed** in
  20.45 seconds.
- `scripts/verify.sh sanitizers`: **38 enabled tests passed** under the macOS
  LLVM 21 UBSan profile in 27 seconds; the package test is intentionally
  disabled in that profile.
- `scripts/check-format.sh`: **47 C++ files passed**.
- `scripts/verify.sh quality`: **failed** during static analysis on the new
  profile test structure. Log:
  `outputs/verification/20260909T034119Z-quality.log`.
- `graphx project graphx.yaml --check --output-dir config`: projections current.
- `git diff --check`: passed.

### Version-1 compatibility fingerprints

| Configuration | Observed SHA-256 | M0 match |
|---|---|---|
| `examples/macvlan/graphx.yaml` | `40e09c1cb7ed78b118efeb10dbfe42fc8b06eb10312091f4df7afd9f0920f69d` | Yes |
| `examples/ipvlan-l2/graphx.yaml` | `da18ff3dfd11a72a834dd6933e161db0a18b92a1ddfc352340208fd274f40f37` | Yes |
| `examples/ipvlan-l3/graphx.yaml` | `598abfa52b274b8d6a3362bc67b141f0852dc590a61b552d99c5960d3b06864b` | Yes |
| `examples/mixed-network/graphx.yaml` | `ede943d05d0e70cd4bb36970bf94be9ea81787568f313766a807a3c14b6b65fc` | Yes |
| `examples/static-route-policy/graphx.yaml` | `33c53b043727d2404f1dcf4a8186dd1ce0b92536b49ead19f20416a166a355db` | Yes |

### Migration and adversarial checks

- Root, macvlan, ipvlan-l2, ipvlan-l3, mixed-network,
  static-route-policy, and qemu-node inputs produced byte-identical repeated
  output and passed the C++ loader.
- A derived valid v1 `ipvlan`/`l3s` input migrated to `profile: ipvlan-l3s` and
  validated.
- Unsupported macvlan `mode: private` and OVS `datapath: netdev` migrations
  failed with bounded diagnostics.
- All seven v2 realization entry points tested failed closed at M3 with no
  legacy `docker network` command.
- Existing file, source path, dangling symlink, duplicate output, and unknown
  option refusal passed in the focused automated test.
- Adversarial unknown attachment switch and wrong mirror output-interface cases
  were incorrectly accepted, establishing M2-V-001.
- Ambient `GRAPHX_OVERRIDES=version=2` incorrectly changed migration behavior,
  establishing M2-V-002.

### Lima ARM64 Linux

- `infrastructure/lima/verify.sh`: **passed** using real system OVS, namespace,
  veth, TAP, packet capture, rootful Docker, cleanup, projections, and the
  38-test GraphX suite.
- Retained guest evidence:
  `/var/lib/graphx/m1/evidence/20260909T033945Z-b6191384`.
- Ubuntu `python3-jsonschema` validated both repeated outputs for all seven
  representative migrations: **14/14 documents accepted**.
- The guest schema rejected a string-valued version while the C++ loader
  accepted it, confirming M2-V-003 across implementations.
- Final Lima instance state: **Stopped**. No M2 runtime realization was
  attempted or claimed.

The M1 primitive packet exchange is regression evidence for the execution
foundation only. It is not evidence for any M2 semantic profile; packet-level
profile realization remains correctly deferred to later milestones.

## Remediation order

1. Fix attachment-to-switch/router/mirror consistency and add adversarial tests
   (M2-V-001).
2. Make migration load the literal file independently of ambient overrides and
   test that boundary (M2-V-002).
3. Align strict `version` typing between the C++ loader and JSON Schema
   (M2-V-003).
4. Restore the static-analysis gate and strengthen exact profile-contract tests
   (M2-V-004 and M2-V-005).
5. Freeze an M2 verifier prompt, rerun the clean portable, quality, sanitizer,
   migration, fingerprint, and Lima/schema checks, then update the phase status
   only if every blocking finding is closed.
