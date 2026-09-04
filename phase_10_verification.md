# Phase 10 independent verification report

Verification date: 2026-09-03 EDT / 2026-09-04 UTC  
Repository: `/Users/rklinkhammer/workspace/graphx-docker`  
Verified revision: `d6f57d0125593e07b76d20282baef1e795150067` plus the uncommitted Phase 10 changes  
Work package: release engineering, compatibility policy, packaging, and support processes

## 1. Verdict

**ACCEPTED**

The Phase 10 acceptance criteria are met in the implementation and in every
locally available verification environment. The previously reported P2 package-
integrity defect is closed: the release verifier now requires the exact 69-file
native payload and exact portable modes, and independent adversarial tests could
not bypass that contract after candidate metadata was self-consistently resealed.

Two fresh macOS arm64 Release/C++20 candidates passed clean configure/build,
18/18 CTests, downstream installation and consumption, genuine-package negative
tests, and trusted-source verification. All four candidate files were byte-
identical. Linux C++20/C++23, ASan/UBSan, formatting, static analysis, fuzz,
telemetry, web, topology, transport, TLS, Compose, container build, and container-
hardening checks also passed.

No production code was changed during this verification. This report is the
only repository file modified by the verifier. Nothing was committed, tagged,
pushed, published, deployed, or changed in GitHub or GHCR.

## 2. Executive summary

- `archive_file_contract()` is an explicit, trusted contract for all 69 regular
  files: 16 headers, 29 Markdown documents, the library/export/configuration and
  license payloads, four applications, and extcap.
- The verifier requires exact `0755` modes for the four applications and extcap,
  and exact `0644` modes for every other regular file. Missing, unexpected,
  linked, special, colliding, unsafe, or incorrectly permissioned content fails.
- The Release package test compares the complete real install tree with the
  contract. It then removes each promised file and strips each program's execute
  mode from genuine CPack output one at a time; every mutation is rejected.
- An independent, fully resealed candidate challenge confirmed rejection of a
  missing `include/graphx/config.hpp`, a `0644` application, a setuid application,
  and an injected unexpected file.
- The macOS-only CPack canonicalization removes AppleDouble metadata without
  changing normal installation. Fresh output had 69 regular files, no `._*`
  entries, and only the contracted `0644` and `0755` modes.
- Two fresh native candidates were byte-identical. The archive SHA-256 was
  `7f718cc51ff546574131c286c704503157340f6e684a4de79f444b78e54cb8aa`.
- No Phase 1-9 behavior or compatibility surface changed, and broad native and
  Linux regression gates passed.
- No P0, P1, P2, or P3 implementation finding remains from this verification.
  Hosted publication state transitions, amd64 output, and live repository
  settings remain operational rehearsal items, not locally provable properties.
- Both direct npm production audits were attempted with a 30-second bound. The
  registry advisory endpoint timed out, so current advisory status is explicitly
  unverified rather than reported as passing. Locked installs, application tests,
  and affected image builds did pass.

## 3. Acceptance-criteria matrix

| Criterion | Result | Implementation evidence | Independent validation evidence |
|---|---|---|---|
| One canonical numeric version drives native, JavaScript, tag, manifest, and OCI identities | PASS | `VERSION`, generated `version.hpp`, package manifests, `source_version()` | Release-contract tests and both fresh manifests report `0.1.0`/`v0.1.0` |
| Installable native package and downstream CMake consumption | PASS | `CMakeLists.txt`, `cmake/GraphXConfig.cmake.in`, `tests/test_package.py` | Both fresh candidates and Linux C++20/C++23 package/consumer tests passed |
| Exact, independent native package inventory | PASS | `ARCHIVE_PUBLIC_HEADERS`, `ARCHIVE_DOCUMENTATION_FILES`, `ARCHIVE_DATA_FILES`, `ARCHIVE_EXECUTABLE_FILES`, and `archive_file_contract()` | Contract equals the source/install sets: 69 files, including 16 headers and 29 docs; real archive contains exactly 69 regular files |
| Exact portable modes and safe archive structure | PASS | `inspect_archive()` retains `mode & 0o7777`; `verify_release_archive()` enforces exact modes and rejects unsafe members | Real archive has only `0644`/`0755`; wrong `0644`, setuid `4755`, missing-file, and extra-file candidates were rejected |
| Non-circular package regression coverage | PASS | `tests/test_package.py` compares a real install tree and mutates genuine CPack output | Every required file omission and every executable-mode strip was rejected during both fresh candidate builds and Linux Release packaging |
| macOS output is free of AppleDouble sidecars | PASS | `cmake/GraphXCpackCanonicalize.cmake`; `COPYFILE_DISABLE` release environment | Fresh archive inspection found zero `._*` entries; exact-set verification passed |
| Canonical four-file candidate and same-toolchain reproducibility | PASS | `scripts/release/build_release.py` | All archive, SPDX, manifest, and checksum files matched byte-for-byte across two isolated builds |
| Candidate metadata binds trusted tag, commit, platform, epoch, and artifacts | PASS | Manifest schema 2 and mandatory trusted verifier inputs | Both candidates passed `verify_release.py --source .`; independent resealing could not bypass the semantic archive contract |
| Native and OCI inventories accurately describe their declared scopes | PASS | Native SPDX generator/validator; digest-bound BuildKit SPDX validation in release workflow | Current native SPDX passed project verification. The immediately preceding independent run validated the same generator against SPDX 2.3 and validated digest-bound arm64 OCI inventories (104 runtime and 221 telemetry packages, including `ws`) |
| Untrusted release inputs are bounded before/during expensive work | PASS | Bounded regular-file reads, JSON maps, tar members, member size, expanded size, captures, collision checks | Release adversarial suite passed; targeted full-candidate challenges stayed within the public verification path |
| OCI identities are non-replacing and partial publication is compensated | PASS BY INSPECTION/UNIT TEST | Absence checks, staging tags, digest checks, `publish != success` rollback, rollback outside release environment | Release tests and workflow inspection passed; hosted state transitions remain an operational rehearsal item |
| Release inputs, base images, scanner, and actions are pinned and updateable | PASS | Digest/commit pins and `.github/dependabot.yml` coverage | Workflow and policy inspection; workflow YAML parsed successfully |
| Release, support, compatibility, upgrade, and test documentation matches behavior | PASS | `README.md`, `CHANGELOG.md`, `SECURITY.md`, `SUPPORT.md`, `CONTRIBUTING.md`, `docs/compatibility-policy.md`, `docs/release-process.md`, `docs/upgrade.md`, `docs/test-procedure.md`, ADR 0011 | Claims were compared with the exact contract, package tests, release scripts, and workflow behavior |
| Existing Phase 1-9 contracts remain compatible | PASS | No wire/configuration/API/persistence/runtime contract change in remediation | Native and Linux regression, TLS, lifecycle, protocol, topology, telemetry, web, capture, Compose, and hardening gates passed |

## 4. Findings

No P0, P1, P2, or P3 implementation findings remain.

The prior P2 finding, “complete-archive contract is incomplete and mode-blind,”
is closed. Its missing regressions now exist at both the synthetic contract layer
and the real CPack-output layer. Documentation now describes the enforced exact
inventory and mode behavior.

Non-finding observations:

- Vite continues to report a non-fatal large-chunk warning for the approximately
  1.85 MB console bundle. This predates and does not block the release-engineering
  milestone.
- The Debian/GCC 12 image build emits existing `-Wrestrict` diagnostics in
  `src/observability.cpp`; the build completes, while Clang 18 warnings-as-errors,
  clang-tidy, cppcheck, ASan, and UBSan are clean. This was not introduced by the
  Phase 10 package-contract remediation.
- The npm advisory service timeout and hosted-release restrictions are recorded
  in section 6. They are environmental unknowns, not evidence of an implementation
  defect.

## 5. Tests and checks run

### Fresh native release candidates

The verifier created two isolated candidates with the equivalent command shape:

```text
python3 scripts/release/build_release.py --source . \
  --build-dir <isolated-root>/build --output-dir <isolated-root>/output \
  --tag v0.1.0 --allow-dirty
python3 scripts/release/verify_release.py <isolated-root>/output --source .
```

`--allow-dirty` was necessary only because the inspected Phase 10 work is not
committed; the publication workflow does not permit this development override.

| Check | Exact result |
|---|---|
| Candidate A | PASS; `/tmp/graphx-phase10-final-verifier-a.Lnulh5`; 18/18 CTests in 25.28 s; package and trusted-source verification passed |
| Candidate B | PASS; `/tmp/graphx-phase10-final-verifier-b.ADIqqc`; 18/18 CTests in 25.52 s; package and trusted-source verification passed |
| Byte reproducibility | PASS; all four candidate files matched with `cmp` |
| Manifest SHA-256 | `eda70a5f58617bb25caea491cb02d0bf4b259207895f22a2948a2aa273fdf74b` |
| Checksums SHA-256 | `9c3a7573917ca51b60a2ae8a326a5e4b6c0fec1a683c2d188eebb98fb99c854d` |
| Native SPDX SHA-256 | `a2559cf1b6f490d7f924375c4edacf024367e392adb5cd1b1a7df6b57f1ab2d2` |
| Archive SHA-256 | `7f718cc51ff546574131c286c704503157340f6e684a4de79f444b78e54cb8aa` |
| Archive inspection | PASS; exactly 69 regular files; zero AppleDouble entries; modes exactly `0644` and `0755` |

### Targeted adversarial verification

A copy of a genuine candidate was modified and all affected archive digest,
native SPDX checksum/namespace, manifest fields, and checksum-file data were
recomputed before invoking the public verifier. Results:

| Mutation | Exact result |
|---|---|
| Remove `include/graphx/config.hpp` | REJECTED: missing required release archive file |
| Change `bin/graphx` to `0644` | REJECTED: incorrect archive mode |
| Change `bin/graphx` to setuid `4755` | REJECTED: incorrect archive mode |
| Add an unexpected regular file | REJECTED: unexpected release archive file |

The harness completed with `FULL_CANDIDATE_ADVERSARIAL_RESEAL=PASS`.

### Linux, web, deployment, and static gates

| Command/check | Exact result |
|---|---|
| `GRAPHX_FUZZ_SECONDS=5 scripts/test-linux-container.sh quality` | PASS; 39-file format check, clang-tidy-18, cppcheck, sanitizer-coverage checks, and both fuzzers for 5 s; `outputs/linux-container/quality-20260904T022240Z.log` |
| `scripts/test-linux-container.sh sanitizers` | PASS; Clang 18 ASan/UBSan; 18/18 enabled tests in 10.72 s; package test disabled by design for sanitizer-linked static archives; `outputs/linux-container/sanitizers-20260904T022410Z.log` |
| `scripts/test-linux-container.sh portable` | PASS; GNU C++23 18/18 in 9.42 s and C++20 18/18 in 9.90 s; all topology, TCP/shared-memory, shutdown, controls, HTTPS, and TLS checks passed; `outputs/linux-container/portable-20260904T022436Z.log` |
| Telemetry via portable suite | PASS; 70/70 tests |
| Web via portable suite | PASS; 9/9 tests and Vite production build |
| `docker compose config --quiet` | PASS |
| `docker compose --profile observability config --quiet` | PASS |
| `bash scripts/test-container-hardening.sh` | PASS; capture ownership and read-only collector behavior in both initialization orders |
| `docker compose --profile observability build` | PASS; demo/runtime and telemetry images built |
| Release Python compilation | PASS: `python3 -m compileall -q scripts/release tests/test_release.py tests/test_package.py` |
| Focused release contract | PASS: `GraphX release contract validation passed` |
| CI/release workflow YAML parse | PASS |
| `git diff --check` | PASS |
| Telemetry production `npm audit` | UNVERIFIED; bounded request failed after 30 s because the npm bulk advisory endpoint timed out |
| Web production `npm audit` | UNVERIFIED; bounded request failed after 30 s because the npm bulk advisory endpoint timed out |

## 6. Unverified areas and why

- No Git tag, GitHub release, GHCR tag, or external deployment was created. The
  verifier is prohibited from mutating those systems. Live success, failure,
  cancellation, environment rejection, compensation, cleanup failure,
  concurrency, and retry behavior require a disposable GitHub/GHCR rehearsal.
- GitHub environment reviewers, tag protection, package-admin access, token
  permissions, and package-deletion behavior are external settings and cannot be
  proven from repository code.
- Local native and container execution was arm64. Hosted Linux x86_64 native
  output and amd64/multi-architecture OCI output were not executed locally.
- The current native SPDX file passed the project's strict verifier. The official
  SPDX 2.3 schema validation and local digest-bound OCI attestation generation
  were not repeated after the final package-contract-only remediation. The
  immediately preceding independent verification passed both checks, and no
  SBOM generator, Dockerfile, lockfile, workflow attestation, or OCI validation
  logic changed in that remediation.
- Direct telemetry and web production audits were attempted with
  `npm_config_fetch_timeout=30000` and no retries. Both failed because
  `https://registry.npmjs.org/-/npm/v1/security/advisories/bulk` timed out.
  Locked installations, tests, and builds passed, but current advisory status is
  not marked verified.
- The worktree is uncommitted. Fresh candidates therefore used the documented
  development-only dirty-tree override; actual publication still requires a
  clean tagged commit.

These limits do not require a `BLOCKED` verdict. All minimum locally feasible
verification ran, the changed package boundary was exercised independently and
adversarially, and the unavailable items are hosted/external operational gates
that the release workflow is designed to rerun.

## 7. Compatibility and security assessment

The canonical version remains 0.1.0. No wire encoding, configuration version,
transport result, lifecycle, telemetry/control API, history schema, capture
format, dissector/extcap contract, or GUI contract changed. Native installation
and downstream consumption pass on tested macOS and Linux arm64 systems, and no
Phase 1-9 regression was detected.

The release trust boundary now has layered controls: trusted expected identity;
an exact four-file candidate; bounded file, JSON, and tar processing; rejection
of traversal, links, special files, duplicates, collisions, oversized content,
missing files, unexpected files, and unsafe modes; artifact-scoped native SPDX;
digest-bound OCI SPDX/provenance; pinned actions, images, and scanner; locked npm
dependencies; non-root runtime containers; non-overwriting publication; and
rollback on every ordinary non-success publication result.

The exact file/mode manifest is independent of the archive under inspection.
Consequently, recomputing hashes and metadata after removing or weakening a
payload cannot make a semantically incomplete candidate valid. This directly
closes the last Phase 10 package-integrity gap without altering product-runtime
semantics.

## 8. Required remediation before acceptance

None. Phase 10 is accepted.

Before a real public release, complete the operational gates that cannot be
performed by this local verifier:

1. Obtain successful telemetry and web production advisory scans from hosted CI
   or a functioning npm advisory endpoint.
2. Confirm GitHub release-environment reviewers, protected tags, least-privilege
   workflow tokens, and GHCR package-admin/delete permissions.
3. Run hosted Linux x86_64/macOS arm64 native and amd64/arm64 OCI jobs.
4. Rehearse publish, failure, cancellation, compensation, and retry in a
   disposable fork and registry namespace before publishing version 0.1.0.

These are release-execution prerequisites, not missing Phase 10 implementation.

## 9. Readiness for the next work package

**Ready.** Phase 10 is the final defined implementation phase; no Phase 11 should
be invented. The next work package is a controlled first-release rehearsal in a
disposable GitHub/GHCR namespace, followed by the normal production release only
after every hosted gate succeeds.

That rehearsal must preserve these invariants:

- GraphX remains transport- and deployment-neutral.
- Wire, configuration, API, persistence, capture, and package compatibility are
  independently versioned and changed only through documented policy.
- Candidate identities are immutable, reproducible, bounded, independently
  verifiable, and never overwritten.
- Native archives retain the exact 69-file and `0644`/`0755` contract.
- OCI attestations remain digest-bound and cover both runtime and telemetry
  images on every published architecture.
- Publication failure or cancellation leaves no final or staging identities,
  and retry begins from a demonstrably absent state.
- Existing Phase 1-9 security, lifecycle, observability, history, control, and
  PCAPNG/Wireshark/extcap gates remain mandatory.
