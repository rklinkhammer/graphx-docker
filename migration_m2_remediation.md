# Migration M2 remediation handoff

**Recorded:** 2026-09-09 (America/New_York)  
**Repository:** `~/workspace/graphx-docker`  
**Source report:** `migration_m2_verification.md`  
**Implementation state:** uncommitted and ready for independent re-verification

## Outcome

All six findings from the first independent M2 verification have been
remediated. Strict attachment validation now resolves the OVS model without
contradictory intent, source migration is independent of runtime overrides,
the loader and schema agree that `version` is an integer, exact semantic
profile behavior is tested, the active M2 contracts are current, and all local
and Lima gates pass.

This is an implementation handoff. It does not replace the independent verdict
in `migration_m2_verification.md`; a separate verifier must re-evaluate the
working tree before M2 is accepted.

## Finding closure matrix

| Finding | Remediation | Regression evidence | State |
|---|---|---|---|
| M2-V-001 | Every attachment switch must resolve. A `namespace_veth` must match exactly one owner-router interface across network, address, device/interface, peer, and switch. A `mirror` must match both the switch mirror ID and the interface resolved through its output port. | C++ negative cases cover unknown switch and each namespace/mirror mismatch. | Remediated |
| M2-V-002 | Added a literal configuration-loading API and used it for source-to-source migration, excluding environment and CLI overrides. | C++ and CLI tests migrate with hostile `GRAPHX_OVERRIDES=version=2` and require byte-identical output. | Remediated |
| M2-V-003 | Version parsing now requires an unsigned YAML integer scalar. Quoted numeric strings are rejected consistently with JSON Schema. | C++ negative test plus guest schema/CLI coverage. | Remediated |
| M2-V-004 | Initialized the profile-case enum member and reformatted touched C++. | Complete quality/static-analysis profile passes. | Remediated |
| M2-V-005 | Profile tests now compare all nine exact fields for all five profiles; CLI inspection checks the same complete behavior strings. | `graphx-config-tests` and `graphx-config-v2` pass on macOS and Linux. | Remediated |
| M2-V-006 | Replaced the active implementation and verifier prompts with phase-specific M2 contracts, including the new adversarial and literal-migration requirements. | Documentation consistency test passes. | Remediated |

## Implementation surface

- `include/graphx/config.hpp` and `src/config.cpp`: literal loading, strict
  integer version parsing, and attachment-to-OVS consistency validation.
- `src/migration.cpp`: migration uses literal source semantics.
- `tests/test_config.cpp` and `tests/test_config_v2.py`: adversarial attachment,
  hostile environment, quoted-version, and exact profile behavior coverage.
- `prompt/implement.md` and `prompt/verifier.md`: active M2 implementation and
  independent verification contracts.
- `README.md`, `docs/configuration-v2.md`, `docs/upgrade.md`,
  `docs/security.md`, and `docs/GraphX_Architecture.md`: documented the literal
  migration boundary. The generated architecture DOCX was refreshed and all
  23 rendered pages were visually inspected.

## Fresh verification evidence

### macOS

- `scripts/verify.sh quick`: **38/38 passed**. Log:
  `outputs/verification/20260909T151318Z-quick.log`.
- `scripts/verify.sh quality`: **passed**, including clang-format, clang-tidy,
  and cppcheck. Log:
  `outputs/verification/20260909T125626Z-quality.log`.
- `scripts/verify.sh sanitizers`: **38 enabled tests passed** under the macOS
  LLVM 21 UBSan profile; the package test is intentionally disabled in this
  profile. Log:
  `outputs/verification/20260909T160323Z-sanitizers.log`.
- Focused configuration, migration, and documentation consistency tests:
  **3/3 passed**.
- Repository formatting: **47 C++ files passed**.
- `graphx project graphx.yaml --check --output-dir config`: projections current.
- `git diff --check`: passed.

The macOS sanitizer policy uses UBSan because AddressSanitizer is unavailable
or hangs on macOS 26 in this environment; Linux retains the combined
AddressSanitizer and UndefinedBehaviorSanitizer profile.

### Version-1 compatibility

The exact `graphx infra create CONFIG --dry-run` SHA-256 fingerprints remain:

| Configuration | SHA-256 |
|---|---|
| `examples/macvlan/graphx.yaml` | `40e09c1cb7ed78b118efeb10dbfe42fc8b06eb10312091f4df7afd9f0920f69d` |
| `examples/ipvlan-l2/graphx.yaml` | `da18ff3dfd11a72a834dd6933e161db0a18b92a1ddfc352340208fd274f40f37` |
| `examples/ipvlan-l3/graphx.yaml` | `598abfa52b274b8d6a3362bc67b141f0852dc590a61b552d99c5960d3b06864b` |
| `examples/mixed-network/graphx.yaml` | `ede943d05d0e70cd4bb36970bf94be9ea81787568f313766a807a3c14b6b65fc` |
| `examples/static-route-policy/graphx.yaml` | `33c53b043727d2404f1dcf4a8186dd1ce0b92536b49ead19f20416a166a355db` |

### Lima ARM64 Linux

- `infrastructure/lima/verify.sh`: **passed** system OVS, namespace/veth/TAP,
  nftables, netem, capture, rootful Docker, projections, cleanup, and all 38
  GraphX tests.
- Retained guest evidence:
  `/var/lib/graphx/m1/evidence/20260909T160841Z-93618588`.
- The guest CLI migration suite passed.
- Ubuntu Draft 2020-12 JSON Schema validation accepted the seven repository
  migrations plus a derived `ipvlan-l3s` migration. The seven standard
  migrations were generated with hostile `GRAPHX_OVERRIDES=version=2`.
- Final Lima instance state: **Stopped**.

## Remaining boundary

M2 still defines and validates intent only. OVS lifecycle, container veth
realization, QEMU TAP realization, and packet-level proof remain M3, M4, M6,
and later work. No M2 runtime realization is claimed.

## Re-verification focus

An independent verifier should rerun `prompt/verifier.md`, reproduce all
attachment mismatch cases, verify literal migration with hostile overrides,
compare CLI and schema rejection of quoted versions, inspect the exact profile
table, rerun the portable/quality/sanitizer/Lima gates, and issue a new verdict
without modifying this handoff or the original verification report.
