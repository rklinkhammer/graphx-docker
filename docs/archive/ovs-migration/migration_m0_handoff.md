# Migration M0 implementation handoff

**Completed:** 2026-09-08  
**Scope:** architecture decisions, migration sequencing, and frozen baseline  
**Runtime behavior changed:** no

## Outcome

M0 establishes the contract for an in-place GraphX migration. ADR 0016 accepts
Lima as the macOS Linux execution boundary. ADR 0017 accepts OVS as the only new
network backend, configuration version 2 as the semantic boundary, veth for
managed containers and namespaces, and TAP for QEMU. The M0-M8 roadmap is now
the authoritative sequence for that migration, while the earlier feature
roadmap remains active for non-conflicting work.

The active implementer and verifier prompts now cover M1 only. They explicitly
forbid claiming version 2, container-veth application paths, QEMU TAP, or KVM
capability before those later gates pass.

## Acceptance matrix

| ID | Requirement | Evidence | Status |
|---|---|---|---|
| M0-001 | macOS execution decision | `docs/adr/0016-lima-ovs-runtime.md` | Implemented |
| M0-002 | OVS/profile/version decision | `docs/adr/0017-ovs-semantic-network-profiles.md` | Implemented |
| M0-003 | Ordered migration plan | `prompt/ovs_migration_implementation_plan.md` | Implemented |
| M0-004 | Frozen pre-change evidence | `migration_m0_baseline.md` | Implemented |
| M0-005 | Next-phase contracts | `prompt/implement.md`, `prompt/verifier.md` | Implemented |
| M0-006 | Documentation consistency | ADR index, architecture source/DOCX, roadmap status, verification status, changelog | Implemented |
| M0-007 | No product behavior change | no schema, C++, CLI, Compose, example, or runtime implementation changed | Implemented |

## Baseline and validation

Before M0 edits, the worktree was clean at
`2280ba3387f036f08acc79e9ac54613d2680d807` on `main`. GraphX reported version
1.1.0 on macOS 26.6.2 arm64.

The pre-change quick profile rebuilt GraphX and passed all 36 tests. Projection
checking and focused version-1 example validation passed. Exact dry-run hashes
are retained in `migration_m0_baseline.md`.

After the documentation changes, the architecture DOCX was regenerated from
the Markdown source, rendered to 23 page images, and visually inspected.
`scripts/verify.sh quick` then rebuilt the project and passed all 36 tests in
20.09 seconds; the full quick profile completed in 25 seconds. The log is
`outputs/verification/20260909T001432Z-quick.log`. Documentation consistency,
projection drift, current network configurations, QEMU, SDR, and route-policy
portable checks all passed. Native Linux and Lima runtime tests are
intentionally not M0 gates.

## Changed surfaces

- two new accepted ADRs and an expanded ADR index;
- a frozen M0 baseline and this implementation handoff;
- the M0-M8 migration plan;
- M1 implementer and independent-verifier contracts;
- architecture roadmap/source map and regenerated DOCX;
- historical feature-roadmap supersession notes;
- Phase 14 documentation links redirected to its completed verification report;
- verification status, changelog, and ADR-count consistency test.

## Remaining work

M1 must create and independently verify the real Lima environment. M0 provides
no runtime evidence for Lima, system OVS, veth, TAP, nftables, netem, capture,
or QEMU acceleration.
