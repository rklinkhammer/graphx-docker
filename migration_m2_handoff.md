# Migration M2 implementation handoff

**Recorded:** 2026-09-08 (America/New_York)  
**Repository:** `~/workspace/graphx-docker`  
**Branch/base:** `main` at `0b6150056a3dbea1f0a5404a006c4d45231f5ecb` (`M0 baseline`)  
**Product:** GraphX 1.1.0  
**Implementation state:** uncommitted for independent review

> The first independent verification found six issues. They have been addressed
> in the working tree; see `migration_m2_remediation.md` for the closure matrix
> and fresh gate evidence. The original verification report remains unchanged as
> a historical record.

## Verdict

Migration M2 is implemented. GraphX now accepts strict configuration versions
1 and 2, models the five fixed OVS semantic profiles and five typed attachment
kinds, and provides a deterministic `graphx config migrate` command. Version 1
retains its existing Docker-driver meaning and all five frozen infrastructure
plan fingerprints. Version-2 infrastructure, route, and fault operations fail
closed until M3; M2 makes no container-veth, QEMU-TAP, or packet-semantics
runtime claim.

The macOS quick profile passed all 38 tests. The retained ARM64 Lima environment
also passed its complete system-OVS/veth/TAP and 38-test cycle, and the guest
JSON Schema validator accepted seven independently emitted migration results.
The VM is stopped with evidence retained.

## Acceptance matrix

| ID | Requirement | Implementation and evidence | Status |
|---|---|---|---|
| M2-001 | Explicit compatibility boundary | The loader and schema accept only versions 1 and 2. Version-aware strict key sets prevent a v2 file from carrying `driver`, `parent`, `mode`, `network.interfaces`, or `deployment.network`. | Implemented |
| M2-002 | Fixed semantic profiles | `ethernet`, `macvlan`, `ipvlan-l2`, `ipvlan-l3`, and `ipvlan-l3s` each expose fixed MAC identity, learning, filtering, ARP, broadcast, multicast, routing, isolation, and management behavior. | Implemented |
| M2-003 | Typed attachments | `container_veth`, `namespace_veth`, `qemu_tap`, `external`, and `mirror` have strict fields, reference checks, owner/runtime rules, address membership checks, and mirror-switch validation. | Implemented |
| M2-004 | Deterministic migration | `graphx config migrate` validates v1 first, produces byte-identical YAML, maps every current network family, creates typed attachments, removes Compose data-plane membership, and refuses ambiguous inputs. | Implemented |
| M2-005 | Safe output | Migration writes to standard output by default. File output uses exclusive no-follow creation, refuses the source, existing paths, dangling symlinks, duplicate output arguments, and unknown options, and removes a partial file after a write failure. | Implemented |
| M2-006 | Version-1 compatibility | All existing v1 config tests and projections pass. The five M0 dry-run SHA-256 fingerprints are unchanged. | Implemented |
| M2-007 | Fail-closed realization | Version-2 infrastructure, route, and fault requests return an M3 boundary diagnostic without generating legacy Docker commands. | Implemented |
| M2-008 | Schema and projections | The JSON Schema has separate v1/v2 network definitions. Inspection prints the OVS backend, profile behavior, and attachments; projection emits version-appropriate fields. | Implemented |
| M2-009 | Documentation | Configuration, migration, upgrade/rollback, support, security, README, architecture source, and editable architecture edition describe the M2 boundary. | Implemented |
| M2-010 | Portable and Linux gates | macOS and Lima passed 38/38 CTest entries; guest JSON Schema validation accepted seven migrated examples. | Implemented |

## Changed paths

- `include/graphx/config.hpp`, `include/graphx/network.hpp`, and
  `src/network.cpp` - configuration boundary, semantic-profile model, attachment
  model, and fixed behavior table.
- `src/config.cpp` - version-aware strict parsing and semantic validation.
- `include/graphx/migration.hpp` and `src/migration.cpp` - deterministic v1-to-v2
  migration.
- `apps/cli/main.cpp` - migration command, safe output, and v2 inspection.
- `apps/cli/projection.cpp` - version-aware network/attachment projections.
- `src/infra.cpp` - explicit M3 fail-closed boundary for v2 realization.
- `config/schema/graphx.schema.json` - strict versioned schema surfaces.
- `tests/test_config.cpp`, `tests/test_config_v2.py`, and `CMakeLists.txt` - model,
  migration, CLI safety, compatibility, and fail-closed tests.
- `docs/configuration-v2.md`, `docs/upgrade.md`, `docs/security.md`, `README.md`,
  and `SUPPORT.md` - operator and security contracts.
- `docs/GraphX_Architecture.md`, `scripts/generate-architecture-doc.py`, and
  `docs/GraphX_Architecture.docx` - synchronized architecture edition.
- `prompt/ovs_migration_implementation_plan.md` - M2 implementation status.
- `migration_m2_handoff.md` - this record.

No version-1 configuration, Compose file, network example, infrastructure
command sequence, QEMU profile, or Lima definition was modified.

## Migration behavior

The command maps legacy intent as follows:

| Version 1 | Version 2 |
|---|---|
| `driver: bridge` | `profile: ethernet` |
| `driver: macvlan` | `profile: macvlan` |
| `driver: ipvlan`, `mode: l2` | `profile: ipvlan-l2` |
| `driver: ipvlan`, `mode: l3` | `profile: ipvlan-l3` |
| `driver: ipvlan`, `mode: l3s` | `profile: ipvlan-l3s` |
| `parent` | `uplink` |
| deployed service interface | `container_veth` attachment |
| Linux namespace router interface | `namespace_veth` attachment |
| QEMU node interface | `qemu_tap` attachment intent |
| other graph-node interface | `external` attachment |
| OVS mirror output | `mirror` attachment |
| `deployment.network` | removed; management remains separate from the data plane |

The migration command was run twice for the root, macvlan, ipvlan-l2,
ipvlan-l3, mixed-network, static-route-policy, and qemu-node configurations.
Each pair was byte-identical, each migrated document passed the C++ loader, and
all seven passed the Draft 2020-12 JSON Schema validator in Ubuntu.

## Compatibility evidence

The exact version-1 `graphx infra create CONFIG --dry-run` SHA-256 values remain:

| Configuration | SHA-256 |
|---|---|
| `examples/macvlan/graphx.yaml` | `40e09c1cb7ed78b118efeb10dbfe42fc8b06eb10312091f4df7afd9f0920f69d` |
| `examples/ipvlan-l2/graphx.yaml` | `da18ff3dfd11a72a834dd6933e161db0a18b92a1ddfc352340208fd274f40f37` |
| `examples/ipvlan-l3/graphx.yaml` | `598abfa52b274b8d6a3362bc67b141f0852dc590a61b552d99c5960d3b06864b` |
| `examples/mixed-network/graphx.yaml` | `ede943d05d0e70cd4bb36970bf94be9ea81787568f313766a807a3c14b6b65fc` |
| `examples/static-route-policy/graphx.yaml` | `33c53b043727d2404f1dcf4a8186dd1ce0b92536b49ead19f20416a166a355db` |

`graphx project graphx.yaml --check --output-dir config` also reports all
version-1 projections current.

## Runtime evidence

### macOS

- Development build completed with AppleClang C++23.
- `scripts/verify.sh quick` passed 38/38 tests in 19.72 seconds and completed in
  25 seconds.
- Log: `outputs/verification/20260909T031223Z-quick.log`.
- Focused migration tests cover determinism, all current profile mappings,
  strict parsing, output refusal, symlink refusal, inspection, and fail-closed
  realization.
- The repository-wide clang-format 21 check passed for all 47 C++ files.
- The regenerated architecture DOCX rendered to 23 pages; every page was
  visually inspected without clipping, overlap, broken tables, or missing
  glyphs.

### Lima ARM64 Linux

- Retained instance: `graphx`, Ubuntu ARM64 on `vz`, configuration digest
  `d2109a9bc8cd39737964bd53091a0a9ec8af045ee674491801dc5794254118e9`.
- `infrastructure/lima/verify.sh` passed system OVS, namespace/veth/TAP,
  nftables, netem, capture, rootful Docker, projections, cleanup, and all 38
  GraphX tests.
- Evidence:
  `/var/lib/graphx/m1/evidence/20260909T031633Z-4ef66f85`.
- Ubuntu's `python3-jsonschema` Draft 2020-12 validator accepted all seven
  generated v2 migrations against `config/schema/graphx.schema.json`.
- Repeated stop succeeded; final instance state is `Stopped`.

## Limits and risks

- M2 defines and validates intent only. It does not create OVS bridges, persist
  ownership state, attach veth pairs to container namespaces, or start QEMU with
  TAP. Those are M3, M4, and M6 responsibilities.
- Profile behavior strings are acceptance contracts for later packet tests, not
  proof that current v1 Docker drivers and future OVS realization are packet
  identical.
- A migrated document is reviewable output, not an automatic deployment step.
  Operators must confirm profiles and attachment ownership before adoption.
- The YAML emitter preserves data deterministically but does not preserve source
  comments or original formatting.
- The schema validates structure; the C++ loader remains authoritative for
  cross-reference, ownership, subnet, and runtime semantic checks.
- This is an implementation handoff, not the independent report required by
  `prompt/verifier.md`.

## Independent verification

An independent verifier should inspect the implementation rather than relying
on this handoff. At minimum, rerun:

```bash
scripts/verify.sh quick
./build/dev/graphx project graphx.yaml --check --output-dir config
python3 tests/test_config_v2.py ./build/dev/graphx .
infrastructure/lima/start.sh
infrastructure/lima/verify.sh
infrastructure/lima/stop.sh
```

The verifier should also compare all five v1 fingerprints with
`migration_m0_baseline.md`, migrate the seven representative inputs twice,
validate the outputs with both implementations, and adversarially confirm that
no v2 infrastructure action can emit a version-1 Docker command.
