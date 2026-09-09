# GraphX independent verification work package

Independently verify **Migration M2: configuration version 2 and semantic
network profiles** in `~/workspace/graphx-docker` against ADR 0017,
`migration_m0_baseline.md`, and acceptance identifiers M2-001 through M2-010 in
`prompt/implement.md`. Treat `migration_m2_handoff.md` and any remediation
handoff only as implementation claims. Write the result to
`migration_m2_verification.md` without making product fixes.

## Independence and scope

- Derive the acceptance matrix from ADR 0017 and the implementation contract.
- Preserve unrelated work; do not commit, publish, deploy, or advance to M3.
- Use a fresh out-of-tree build for the portable runtime result.
- Classify static inspection, portable runtime, JSON Schema, Lima guest, and
  not-yet-applicable packet evidence separately.
- M1 OVS/veth/TAP packets prove the Linux foundation, not M2 profile semantics.
- Keep files, commands, waits, and guest resources bounded and return Lima to
  its initial state.

## Verification procedure

### 1. Boundary and semantic model

- Confirm only integer versions 1 and 2 are accepted by both the C++ loader and
  Draft 2020-12 JSON Schema. Reject quoted versions.
- Confirm v1 retains Docker-driver meaning and v2 rejects `driver`, `parent`,
  `mode`, `network.interfaces`, and `deployment.network`.
- Verify all nine exact behavior fields for all five semantic profiles in the
  typed model, documentation, and `graphx inspect` output.
- Confirm v2 accepts only the OVS system datapath.

### 2. Attachment integrity

Exercise all five attachment kinds and their required and forbidden fields.
Adversarially alter each network, address, owner, interface, peer, and switch
reference. In particular:

- a `namespace_veth` must match exactly one interface on its Linux namespace
  router, including network, address, device/interface, peer, and switch;
- every non-empty attachment switch must exist;
- a mirror attachment must match its OVS switch, mirror ID, configured output
  port, and resolved interface;
- container and QEMU owners must match deployment/runtime rules;
- addresses and MACs must pass type and subnet validation.

### 3. Migration and safety

- Migrate all seven representative repository inputs twice and compare bytes.
- Add an explicit ipvlan-l3s migration case.
- Run migration with hostile `GRAPHX_OVERRIDES`, including `version=2`, and
  require identical output to the clean environment.
- Validate every result with both the C++ loader and JSON Schema.
- Confirm unsupported modes, ambiguity, non-system OVS, collisions, and bounds
  fail with actionable diagnostics.
- Verify standard-output behavior, exclusive mode 0600 file creation, and
  refusal of source, existing, symlink, duplicate-output, and unknown options.

### 4. Compatibility and fail-closed behavior

- Run all affected portable tests, the quality and sanitizer profiles, and the
  projection check.
- Recompute all five version-1 infrastructure dry-run SHA-256 values and compare
  them exactly with `migration_m0_baseline.md`.
- Exercise v2 create, destroy, status, route apply/clear, and fault apply/clear.
  Each must stop at M3 and emit no legacy Docker-network command.

### 5. Lima regression and documentation

- Run `infrastructure/lima/verify.sh` once in the real ARM64 Lima guest and
  retain its bounded evidence.
- Validate all migrated documents with the guest JSON Schema implementation.
- Confirm cleanup and return the instance to its initial running/stopped state.
- Verify configuration, migration, upgrade/rollback, security, support,
  architecture, implementation, and verifier documents agree about the M2/M3
  boundary and ambient-override policy.

## Verdict

Report M2-001 through M2-010 as `Implemented`, `Partial`, `Missing`, or `Not Yet
Applicable`, with exact evidence and remediation. M2 passes only when strict
attachment integrity, literal deterministic migration, schema/loader agreement,
all portable quality gates, all frozen v1 fingerprints, and the Lima/schema
regression gate pass. Packet realization of semantic profiles remains not yet
applicable until the later realization milestones.
