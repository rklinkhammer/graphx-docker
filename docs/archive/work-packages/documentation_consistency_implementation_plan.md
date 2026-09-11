# GraphX documentation and consistency implementation plan

**Repository:** `~/workspace/graphx-docker`  
**Roadmap source:** `docs/GraphX_Architecture.md`, section 13  
**Plan status:** Implemented 2026-09-06; see `documentation_consistency_handoff.md`  
**Scope type:** Maintenance work package; no phase number assigned

## 1. Objective

Make GraphX 1.0.0 documentation and checked-in configuration views internally consistent, easy to navigate, and mechanically resistant to drift.

The work package implements the four immediate documentation-and-consistency recommendations:

1. resolve the duplicate ADR number while retaining a documented historical mapping;
2. update root maturity language to match GraphX 1.0.0 and the accepted Phase 3–12 verification record;
3. make the architecture document discoverable from primary project documentation; and
4. generate or verify the four human-readable configuration projections from authoritative `graphx.yaml` in CI.

## 2. Required outcomes

| ID | Outcome | Acceptance summary |
|---|---|---|
| DOC-001 | Unique ADR numbering | Every accepted ADR has one unique canonical number; the former QEMU identifier is recorded in an ADR index and release/history notes. |
| DOC-002 | Accurate maturity statement | Root documentation identifies GraphX 1.0.0 without calling the product merely a scaffold and accurately qualifies the verification record. |
| DOC-003 | Architecture discoverability | Markdown and DOCX architecture documents, their regeneration command, and maintenance responsibility are linked from primary documentation. |
| CON-001 | Deterministic projections | Logical graph, transport, network, and deployment projections are deterministically derived from root `graphx.yaml`. |
| CON-002 | Non-mutating drift check | A documented check exits nonzero and prints a bounded, useful diff when any checked-in projection is stale. |
| CON-003 | CI enforcement | Portable CI/verification invokes the drift check after the required build/dependency preparation. |
| CON-004 | Compatibility | Configuration v1, Compose, runtime behavior, examples, packaging, and existing verification profiles remain unchanged. |
| DOC-004 | Living-document integrity | The architecture DOCX can be regenerated from the Markdown source and passes structural/render checks. |

## 3. Scope

### 3.1 In scope

- `docs/adr/0013-unified-qemu-profiles.md` and a new ADR index.
- Repository references to the QEMU ADR number or path.
- Root `README.md`, `CONTRIBUTING.md`, and narrowly relevant documentation navigation.
- `docs/GraphX_Architecture.md`, `docs/GraphX_Architecture.docx`, architecture diagram assets, and `scripts/generate-architecture-doc.py` only as needed to keep links and numbering current.
- A deterministic projection generator/checker for:
  - `config/graph-topology.yaml`;
  - `config/transport-topology.yaml`;
  - `config/network-topology.yaml`; and
  - `config/deployment-topology.yaml`.
- Automated tests and CI/verification-script integration for projection drift.
- A concise implementation handoff recording commands and results.

### 3.2 Out of scope

- Renumbering historical phases or rewriting accepted ADR decisions.
- Changing configuration version 1, the GraphX wire protocol, runtime behavior, or network realization.
- Generating `compose.yaml` or Kubernetes manifests. Compose remains a separately reviewed static deployment artifact.
- Implementing any near-term example or later architectural capability from the roadmap.
- Retrospectively inventing Phase 1 or independent Phase 2 verification evidence.
- Publishing, tagging, or changing product version 1.0.0.

## 4. Design decisions for this work package

### 4.1 Canonical QEMU ADR number

The QEMU ADR previously occupied a duplicate ADR 0012 path. Canonicalize it at:

```text
docs/adr/0013-unified-qemu-profiles.md
```

Change its title to `ADR 0013` without changing the accepted decision body. Do not retain a second accepted ADR numbered 0012 and do not add a filesystem symlink. Instead, add `docs/adr/README.md` containing the canonical ordered ADR index and this historical mapping:

```text
The unified QEMU profiles decision was initially recorded as ADR 0012.
It is canonicalized as ADR 0013 because ADR 0012 was already assigned to
bounded IPv4 UDP edges.
```

This preserves historical searchability without leaving two current canonical ADRs with the same number. Add a short entry to `CHANGELOG.md` under the current development/documentation section. Update all repository references to the old path or number.

### 4.2 Maturity and verification wording

Replace the root README’s “framework scaffold” status language with wording equivalent to:

> GraphX 1.0.0 is a validated educational framework with a C++ runtime, five GraphX-aware transports, raw external-edge modeling, live telemetry/control, bounded history and PCAPNG capture, a React Flow console, native-Linux network laboratories, and portable/Linux QEMU demonstrations.

Immediately qualify the statement:

- Phases 3–12 have current accepted independent verification reports.
- Phase 1 lacks a checked-in independent report.
- Phase 2 has a completed implementer handoff but no separate independent report.
- Native-Linux-only claims rely on the Linux evidence recorded in the relevant reports.

Link `verification_status.md`; do not imply that all phases have identical documentary evidence.

### 4.3 Documentation navigation

Add a short **Architecture** section near the beginning of `README.md` linking:

- `docs/GraphX_Architecture.md` as the maintained source;
- `docs/GraphX_Architecture.docx` as the editable distribution document;
- `docs/adr/README.md` as the decision index;
- `docs/network-infrastructure.md`, `docs/observability.md`, `docs/capture.md`, and `docs/qemu-demos.md` as focused operational references.

Add a contributor-maintenance subsection to `CONTRIBUTING.md` that states:

1. architecture changes require an ADR or an update to an existing ADR;
2. the Markdown architecture source is authoritative over the generated DOCX;
3. material architecture edits require running `scripts/generate-architecture-doc.py` and visually checking the render;
4. changes to root `graphx.yaml` require regenerating/checking the configuration projections; and
5. generated-document and projection changes must be included in the same pull request as their source change.

### 4.4 Projection generator architecture

Use the already validated C++ `GraphConfig` as the source model rather than introducing a third YAML interpretation. Extend the CLI with a narrowly scoped command:

```text
graphx project [config.yaml] --output-dir DIR
graphx project [config.yaml] --check --output-dir DIR
```

The default source remains `GRAPHX_CONFIG` or `graphx.yaml`. The default output directory is `config` only when invocation is from a repository checkout; explicit `--output-dir` is recommended in tests.

The command must:

- load the source through `graphx::load_config` with the same strict syntax and semantic validation as `validate` and `inspect`;
- produce the four projection files from the normalized `GraphConfig`;
- use deterministic ordering, stable indentation, native YAML scalar types, and a fixed generated-file header;
- use UTF-8 and one trailing newline;
- create temporary files in the destination directory and atomically replace targets only after every projection has been rendered successfully;
- refuse symlink targets and non-regular existing targets;
- create ordinary files with repository-appropriate permissions;
- in `--check` mode, perform no writes and compare normalized expected bytes with checked-in bytes;
- report all stale/missing projections together and return nonzero;
- print a bounded unified diff or an actionable per-file summary without dumping the full configuration;
- reject an output directory that resolves to the source file’s path or an unsafe broad target;
- leave `compose.yaml` untouched.

If implementation evidence shows that adding projection rendering to the CLI would materially complicate the stable C++ package interface, an internal build-time executable is acceptable. It must still reuse `load_config`, provide generate/check modes, and install no public library API.

## 5. Canonical projection contracts

The current projection files are discussion-oriented views, not independently loadable GraphX configurations. Preserve that purpose.

### 5.1 Graph projection

`config/graph-topology.yaml` contains:

- a generated/non-authoritative warning;
- graph ID;
- nodes with ID, kind, runtime, execution, lifecycle, control, optional accelerator/architecture, and ports;
- edges with ID, full source/target port references, transport kind, and data-plane classification.

Do not reduce nodes to an ID-only list; that would discard fields now required by QEMU/external-node architecture.

### 5.2 Transport projection

`config/transport-topology.yaml` contains one deterministic link record per graph edge. Include all semantically relevant settings for that transport, including framing and external data-plane classification. Omit fields that do not apply to the selected transport rather than serializing unrelated defaults.

The generator must support all current transport kinds:

- in-process;
- TCP, including TLS/retry/deadline fields;
- UDP, including mode/destination/bind/interface/TTL/loopback/reuse/buffer/datagram fields;
- Unix-domain socket; and
- shared memory.

### 5.3 Network projection

`config/network-topology.yaml` contains networks, switches/ports/VLANs/mirrors, routers/interfaces/routes/policies, node interfaces, and edge paths. Empty collections may be omitted consistently. Preserve declared order where it carries presentation meaning, especially ordered edge-path hops and router/switch ports.

### 5.4 Deployment projection

`config/deployment-topology.yaml` contains deployment network when configured, managed service placement, and telemetry service/port. It must not fabricate services for external-lifecycle nodes.

### 5.5 Projection header

Each file begins with a stable warning such as:

```yaml
# Generated from ../graphx.yaml by `graphx project`.
# Non-authoritative view: edit graphx.yaml, then regenerate.
```

The relative source path in the checked-in files is fixed. Tests using temporary paths may supply or normalize a display path so host-specific absolute paths never enter the repository.

## 6. Implementation work breakdown

### Work package 0 — Baseline and inventory

**Implementer tasks**

1. Read repository instructions, `docs/GraphX_Architecture.md`, every ADR, README/CONTRIBUTING navigation, root `graphx.yaml`, the four projection files, configuration model, CLI, CMake tests, and CI scripts.
2. Record `git status`, current version, branch/commit, and existing uncommitted changes.
3. Run the current quick verification profile and record pre-existing failures without modifying unrelated work.
4. Search case-insensitively for `0012-unified-qemu`, `ADR 0012`, “framework scaffold,” and all projection filenames.

**Exit criteria**

- Complete affected-file list and baseline result are recorded.
- Existing user changes are preserved.

### Work package 1 — ADR canonicalization

**Implementer tasks**

1. Rename the QEMU ADR to `0013-unified-qemu-profiles.md` using a history-preserving move.
2. Change only the QEMU ADR number/title and any self-reference required by the rename.
3. Add `docs/adr/README.md` with number, title, status, date, path, and the historical-number note.
4. Update repository links and architecture-document references.
5. Add a documentation-only changelog entry.
6. Regenerate the architecture DOCX if its ADR table or source map changes.

**Tests**

- No two canonical ADR index rows share a number.
- Every indexed ADR path exists.
- No live repository link targets the old path.
- A search for the old identifier finds only the explicit historical mapping/changelog note.

### Work package 2 — Maturity and architecture navigation

**Implementer tasks**

1. Replace stale README maturity wording with the approved 1.0.0 statement and documentary qualification.
2. Add the Architecture navigation section and verification-status link.
3. Add contributor maintenance rules.
4. Confirm all relative links resolve from their containing document.
5. Keep quick-start instructions prominent; do not turn the README into a second architecture document.

**Tests**

- Markdown link checker or a repository-local path checker passes.
- README version text matches root `VERSION` and package versions.
- “framework scaffold” no longer appears as current project status.
- Phase verification wording matches `verification_status.md` exactly enough to avoid overclaiming.

### Work package 3 — Projection renderer

**Implementer tasks**

1. Add internal projection-rendering code and CLI parsing for generate/check modes.
2. Keep rendering functions side-effect-free: `GraphConfig -> deterministic strings`.
3. Add a separate file-output layer implementing safe atomic replacement.
4. Generate all four outputs in memory before writing any target.
5. Regenerate the checked-in projections from root `graphx.yaml`.
6. Update README wording that currently calls projection generation a future command.
7. Document the two operator commands in `CONTRIBUTING.md`.

**Required unit tests**

- Deterministic output from repeated renders.
- Root graph content appears in the correct projection and not in unrelated projections.
- Every current transport renders relevant settings.
- QEMU-style node/runtime/data-plane fields render in temporary fixtures.
- Networks, multiple subnets, interfaces, switches, VLANs, mirrors, routers, routes, policies, and ordered edge paths render.
- External nodes do not gain fabricated managed services.
- Strings requiring YAML quoting remain valid and round-trip safely.
- Empty optional fields/collections follow one canonical omission rule.
- Invalid source configuration fails before output mutation.
- Failure to render one projection leaves all existing targets unchanged.
- Symlink and special-file targets are rejected without modifying their referents.

### Work package 4 — Drift checker and CI integration

**Implementer tasks**

1. Add a CTest or repository test named `graphx-config-projections` that invokes `graphx project --check` against root `graphx.yaml` and `config/`.
2. Add an adversarial test that changes one temporary projection and proves check mode fails without rewriting it.
3. Include the check in `scripts/verify.sh quick` after the CLI is built. If quick-profile time is a concern, the check may run through CTest as long as it is guaranteed by quick and portable profiles.
4. Add the check to Linux and macOS CI jobs that build the CLI.
5. Make failure output state the regeneration command exactly.

**Exit criteria**

- A stale projection fails locally and in CI.
- An up-to-date projection passes on Linux and macOS.
- Check mode is non-mutating, including on failure.

### Work package 5 — Living architecture artifact validation

**Implementer tasks**

1. Run `scripts/generate-architecture-doc.py` with the documented bundled/runtime dependencies.
2. Structurally validate the DOCX ZIP package.
3. Render DOCX to page images and inspect every page for clipping, overlap, bad table flow, and unreadable diagrams.
4. Confirm the DOCX contains no placeholder text or internal generation tokens.
5. Decide and document whether CI checks only source/link integrity or also regenerates the DOCX. Avoid byte-for-byte DOCX comparison if package timestamps make it nondeterministic; compare normalized document XML/media hashes if automated equivalence is required.

**Exit criteria**

- Markdown and DOCX show ADR 0013 and valid source paths.
- Rendered pages are visually clean.
- The regeneration command is reproducible and documented.

### Work package 6 — Final regression and handoff

**Implementer tasks**

1. Run formatting/static checks for changed C++/scripts.
2. Run quick and portable verification profiles.
3. Run configuration validation for the root file and all example `graphx.yaml` files.
4. Run package/release tests affected by installed documentation or CLI command changes.
5. Review the complete diff for accidental generated output, host paths, credentials, or unrelated edits.
6. Write a handoff containing outcome IDs, changed files, commands/results, platform limitations, and any remaining remediation.

## 7. Recommended implementation sequence

Use small reviewable changes in this order:

1. ADR rename, ADR index, link updates, and changelog.
2. README maturity/navigation and contributor-maintenance rules.
3. Pure projection rendering functions and unit tests.
4. CLI generate/check behavior and safe output handling.
5. Regenerated checked-in projections.
6. CTest, verification script, and CI enforcement.
7. Architecture DOCX regeneration and final documentation validation.
8. Full regression evidence and handoff.

Do not combine the ADR/documentation cleanup with unrelated runtime or example changes.

## 8. Independent verification plan

The verifier must derive results from repository evidence and runtime commands rather than accepting the handoff as proof.

### 8.1 Baseline and scope

- Record repository status, diff, version, branch, and host/tool versions.
- Confirm unrelated user changes were not removed or rewritten.
- Confirm changes are restricted to documentation, projection tooling/tests, and CI integration.

### 8.2 ADR verification

- Enumerate canonical ADRs from `docs/adr/README.md` and the filesystem.
- Confirm numbers are unique and ordered.
- Confirm the UDP decision remains canonical ADR 0012 and QEMU is canonical ADR 0013.
- Compare the renamed QEMU ADR body with its pre-change version; only identifier/self-link edits are allowed.
- Search for stale old-path links and ambiguous current `ADR 0012` QEMU references.

### 8.3 Maturity and navigation verification

- Compare README claims with `VERSION`, `verification_status.md`, and phase reports.
- Confirm no claim turns missing Phase 1/2 independent records into accepted verification.
- Resolve every new local documentation link.
- Follow contributor commands from a clean temporary output location.

### 8.4 Projection verification

For root `graphx.yaml`:

1. Generate projections into a new temporary directory.
2. Compare them byte-for-byte with checked-in projections.
3. Run check mode and expect success.
4. Change, delete, and add unexpected content to separate temporary projection copies; each check must fail clearly and remain non-mutating.
5. Repeat generation and confirm deterministic bytes.
6. Parse every generated YAML file with an independent YAML parser.

Use temporary fixtures to verify:

- all five GraphX transport kinds;
- raw QEMU-style external edges;
- TLS settings;
- UDP unicast/broadcast/multicast settings;
- multiple subnets;
- OVS ports, access/trunk VLAN metadata, and mirror;
- router interfaces, explicit routes, forwarding, and policies;
- node IP/MAC interfaces and ordered edge paths;
- managed and external deployment nodes;
- quoted YAML edge cases.

Adversarially verify invalid input, missing directories, unwritable destinations, symlinks, FIFOs/special files where supported, partial-write simulation, and interrupted generation. No failure may partially update the checked-in set.

### 8.5 CI and regression verification

- Prove the local quick profile runs the projection drift check.
- Inspect all relevant Linux/macOS workflow jobs for the same enforcement.
- Introduce a temporary stale projection and prove the selected local gate fails.
- Restore the temporary change without altering user work.
- Run quick, portable, formatting/static, configuration, and affected package tests.
- Classify platform-specific skips rather than reporting them as passes.

### 8.6 DOCX verification

- Regenerate the DOCX from Markdown.
- Validate its package structure.
- Render and inspect every page.
- Confirm diagrams, ADR number, source map, headings, tables, links/text, and page flow are correct.

## 9. Acceptance matrix

| ID | Implementation evidence expected | Validation evidence expected | Pass condition |
|---|---|---|---|
| DOC-001 | ADR rename, index, updated references | Unique-number scan and historical body comparison | One canonical UDP 0012 and one QEMU 0013; old identity only in history note |
| DOC-002 | README maturity and verification wording | Comparison with `VERSION` and phase status | Accurate 1.0.0 statement with Phase 1/2 documentary caveat |
| DOC-003 | README/CONTRIBUTING links and rules | Local-link resolution and command walkthrough | Architecture and ADRs discoverable; maintenance steps usable |
| DOC-004 | Regenerated DOCX and generator | ZIP validation and full-page visual inspection | Editable artifact matches maintained source and renders cleanly |
| CON-001 | Pure deterministic renderer and regenerated projections | Repeated temp generation and independent YAML parse | Identical normalized outputs containing every required field |
| CON-002 | `--check` implementation | Stale/missing/adversarial tests | Clear nonzero bounded diagnostic and zero mutation |
| CON-003 | CTest/verify/CI wiring | Local failure injection plus workflow inspection | Drift cannot pass required local or CI gates |
| CON-004 | Focused diff and regression results | Quick/portable/config/package results | No configuration, Compose, runtime, example, or package regression |

## 10. Risks and mitigations

| Risk | Mitigation |
|---|---|
| ADR rename breaks links | Repository-wide link update plus explicit historical mapping in the ADR index and changelog |
| Projection generator becomes a second configuration authority | Always load `GraphConfig` through `load_config`; label outputs non-authoritative |
| Generator serializes defaults that hide author intent | Define projection contracts and omit inapplicable fields; test each transport/device |
| Partial generation leaves mixed revisions | Render all outputs first, validate targets, then atomically replace as one guarded operation with cleanup |
| CLI surface expands the public compatibility contract | Mark command as tooling, test diagnostics, avoid new public C++ library API |
| DOCX comparison is unstable | Keep Markdown authoritative; use render/structural verification or normalized XML/media comparison |
| README overstates verification | Link the status ledger and retain explicit Phase 1/2 documentary caveats |
| CI check is bypassed on one platform | Wire it into shared CTest/verification profiles used by both Linux and macOS jobs |

## 11. Definition of done

The work package is complete only when:

- DOC-001 through DOC-004 and CON-001 through CON-004 pass independent verification;
- the repository has unique canonical ADR numbers and a maintained ADR index;
- current project maturity and verification wording is accurate;
- architecture artifacts are easy to find and reproducibly maintained;
- root projections are generated from the validated authoritative model and protected by a non-mutating CI drift check;
- quick and portable regression profiles pass on their supported environments;
- no runtime, protocol, configuration-version, example, Compose, package, or security behavior changed unintentionally; and
- implementation and independent-verification reports record actual commands, results, skips, and remaining limitations.
