# Phase 10 implementation handoff

Date: 2026-09-03 EDT / 2026-09-04 UTC  
Work package: release engineering, compatibility policy, packaging, and support processes  
Remediation basis: `phase_10_verification.md` (`CHANGES REQUIRED`, P2-1)

## 1. Outcome summary

The remaining Phase 10 package-integrity finding is remediated. The native
release verifier now uses one authoritative inventory containing every installed
regular file, including all 16 shipped GraphX headers and all recursively
installed Markdown documentation. It requires an exact file set and exact
portable modes: `0755` for the four applications and extcap, and `0644` for every
library, header, CMake file, configuration, dissector, license, and document.

Tests compare the real Release install tree to this contract, validate the real
CPack archive, remove every promised file from that archive one at a time, and
strip every executable one at a time. Every mutation is rejected. Synthetic
tests also reject an unexpected file and altered library or executable modes.

The stricter inventory exposed previously hidden macOS AppleDouble `._*` entries.
A macOS-only CPack post-build canonicalization now recreates the staged TGZ with
CMake's archive implementation, which omits extended-attribute sidecars. Two
fresh candidates contain exactly 69 regular files, zero AppleDouble entries,
only the two contracted modes, and are byte-identical across all four artifacts.

No wire, configuration, API, runtime, capture, telemetry, history, control, or
GUI contract changed. Nothing was committed, tagged, pushed, published, or
deployed.

## 2. Requirements implemented

| Requirement | Implementation evidence | Result |
|---|---|---|
| Complete public-header inventory | `ARCHIVE_PUBLIC_HEADERS` in `scripts/release/release_common.py` lists all 15 source `.hpp` files plus generated `version.hpp` | Met |
| Complete installed-document inventory | `ARCHIVE_DOCUMENTATION_FILES` lists every Markdown file installed by the recursive CMake rule | Met |
| Authoritative package contract | `archive_file_contract()` is shared by candidate construction, independent verification, and package tests | Met |
| Exact package contents | `verify_release_archive()` rejects missing and unexpected regular files | Met |
| Portable and safe modes | Contract requires `0755` for applications/extcap and `0644` for every other regular file; inspection preserves and validates tar modes | Met |
| Real-output consistency | `tests/test_package.py` compares every Release install path/mode with the contract and validates the produced TGZ | Met |
| Non-circular negative coverage | The real CPack archive is rewritten with every required file omitted in turn and every executable mode stripped in turn; every variant must fail | Met |
| macOS metadata canonicalization | `cmake/GraphXCpackCanonicalize.cmake` recreates the staged archive without AppleDouble entries after CPack installation | Met |
| Release-only contract scope | Exact release mutations run for Release configurations; Debug still validates installation, downstream consumption, and safe packaging without pretending its debug library/export names are release artifacts | Met |
| Documentation accuracy | README, release process, and test procedure describe exact inventory, modes, unexpected-file rejection, real-output mutations, and macOS handling | Met |
| Existing Phase 10 controls | Trusted identity, bounded parsing, SPDX, reproducibility, pinned inputs, non-overwrite publication, and rollback controls remain unchanged | Preserved |
| Phase 1-9 behavior | Fresh native, Linux sanitizer/quality/fuzz, C++20/C++23, telemetry, web, TLS, transport, and configuration gates pass | Preserved |

## 3. Architecture and compatibility decisions

The package contract is explicit rather than inferred from an untrusted archive.
That makes omission and injection checks independent of the candidate under
inspection. Keeping the inventory in dependency-free `release_common.py` lets
the builder, offline verifier, and tests share one reviewed source of truth.

The contract uses exact `0644` and `0755` modes instead of merely checking an
execute bit. CMake install/CPack already standardizes these portable permissions;
exact validation also rejects group-writable payloads and accidental executable
data. Directory entries and their modes are not semantic package payload and are
not required, but unsafe, duplicate, linked, special, colliding, or out-of-root
entries remain rejected.

Release packages are always configured as Release by `build_release.py`.
Development Debug packages legitimately use `libyaml-cppd.a` and
`GraphXTargets-debug.cmake`, so exact release-inventory assertions are scoped to
Release while the existing Debug install/consumer checks remain active.

macOS provenance extended attributes cannot reliably be cleared from files on
the current host. The post-build script therefore recreates the archive from
CPack's completed staging tree using `cmake -E tar`. This affects only macOS TGZ
packaging, not ordinary installs. `SOURCE_DATE_EPOCH`, `ZERO_AR_DATE`, UTC, and
the C locale continue to provide the reproducibility boundary.

No product compatibility surface changed. The canonical version remains 0.1.0,
and wire/configuration/API/persistence versioning remains independent of package
layout enforcement.

## 4. Files and major components changed

- `scripts/release/release_common.py`: complete file inventory, portable mode
  contract, captured tar modes, exact-set and exact-mode enforcement.
- `tests/test_release.py`: contract-correct fixtures plus wrong-mode,
  group-writable-library, setuid-executable, and unexpected-file negative tests.
- `tests/test_package.py`: real installed-set/mode comparison and genuine CPack
  archive omission/executable-mode mutation coverage.
- `CMakeLists.txt` and `cmake/GraphXCpackCanonicalize.cmake`: macOS CPack
  post-build canonicalization.
- `scripts/release/build_release.py`: explicit `COPYFILE_DISABLE` in the release
  environment as an additional macOS packaging safeguard.
- `README.md`, `docs/release-process.md`, and `docs/test-procedure.md`: exact
  behavior and verification guidance.
- `phase_10_handoff.md`: this updated evidence and acceptance handoff.

All other dirty-worktree content was preserved.

## 5. Tests and checks run

| Check | Exact result |
|---|---|
| Focused release contract | PASS: `GraphX release contract validation passed` |
| Real Release install/package contract | PASS: all 69 installed regular files and modes matched; genuine-archive omission of every file and stripping of every program were rejected |
| Two fresh macOS arm64 Release/C++20 candidates | PASS: 18/18 CTests in 25.85 s and 25.65 s; package test included full real-output mutation coverage; both independent trusted-source verifications passed |
| Candidate reproducibility | PASS: all four artifacts byte-identical; archive SHA-256 `7f718cc51ff546574131c286c704503157340f6e684a4de79f444b78e54cb8aa` |
| Candidate archive inspection | PASS: 69 regular files, zero AppleDouble entries, modes exactly `0644` and `0755` |
| Linux quality/static/fuzz | PASS: 39-file format check, clang-tidy-18, cppcheck, sanitizer coverage, and envelope/frame fuzzers for 5 s each; `outputs/linux-container/quality-20260904T021142Z.log` |
| Linux Clang 18 ASan/UBSan | PASS: 18 enabled tests in 10.58 s; package test disabled by design for sanitizer-linked static archives; `outputs/linux-container/sanitizers-20260904T021231Z.log` |
| Linux C++23 portable | PASS: 18/18 tests in 7.48 s; Debug install/consumer/package behavior validated |
| Linux C++20 portable | PASS: 18/18 tests in 7.45 s; Release exact package inventory and all real archive mutations passed |
| Telemetry and web through portable suite | PASS: telemetry 70/70; web 9/9; Vite production build in 4.62 s with the existing non-fatal large-chunk warning |
| Configuration, TCP/shared-memory, controls, HTTPS/TLS | PASS through the complete portable suite; `outputs/linux-container/portable-20260904T021347Z.log` |
| Compose validation | PASS: base and observability projections |
| Container hardening | PASS: capture ownership and read-only collector checks in both initialization orders |
| Python/workflow/static syntax | PASS: Python compilation, CI/release YAML parsing, and `git diff --check` |
| Direct production npm audit | NO CURRENT COMPLETE RESULT: telemetry audit remained pending for 30 s and was cancelled; the verifier had observed the same advisory-endpoint timeout for both locks. Locked installs and application tests passed, and the telemetry image install in the immediately preceding verifier reported zero vulnerabilities |
| OCI SBOM regeneration | NOT RERUN: no Dockerfile, dependency lock, scanner, workflow, or OCI validation code changed; the immediately preceding verifier validated digest-bound arm64 inventories with 104 runtime and 221 telemetry packages including `ws` |

The first Linux portability attempt correctly failed because the new exact
Release inventory was applied to an intentional Debug build. The test was
corrected to preserve Debug install/consumer coverage while reserving release
artifact assertions for Release builds. The entire portable suite was rerun and
passed; the failed attempt is not counted as final evidence.

## 6. Known limitations

1. No hosted GitHub/GHCR publication, cancellation, compensation, or retry was
   performed. Those external state transitions still require a disposable
   namespace and the documented package-admin settings.
2. Local native and OCI evidence is arm64. Hosted Linux x86_64/macOS arm64 and
   multi-architecture OCI workflow execution remains the release gate.
3. The worktree is uncommitted, so local candidates used development-only
   `--allow-dirty`. Published workflows prohibit it.
4. The npm advisory endpoint did not return a current direct audit result in this
   remediation run. Hosted CI must rerun both production audits.
5. The existing Vite bundle-size warning remains non-fatal and unrelated to this
   package-integrity remediation.

## 7. Risks for the independent verifier

- Compare `archive_file_contract()` with a fresh `cmake --install` tree rather
  than trusting the handoff or synthetic fixtures.
- Delete a header that was absent from the old allowlist, such as `config.hpp`,
  reseal the candidate, and confirm independent verification rejects it.
- Strip the owner execute bit from each of the four applications and extcap;
  also try `0775`, setuid/setgid bits, and a group-writable static library.
- Add an unexpected regular file under the canonical root and confirm rejection.
- Inspect a fresh macOS archive for `._*` AppleDouble entries and compare two
  separate candidates byte-for-byte.
- Confirm Debug package tests do not claim to satisfy the Release artifact
  contract and that Linux Release packaging still enforces it.

## 8. Explicit acceptance-criteria checklist

- [x] All 15 source public headers and generated `version.hpp` are required.
- [x] Every recursively installed Markdown document is required.
- [x] All other installed binaries, libraries, exports, data, tools, licenses,
  and primary documents are required.
- [x] Missing and unexpected regular files fail verification.
- [x] Program/extcap mode is exactly `0755`; every other regular file is exactly
  `0644`.
- [x] Archive inspection retains modes and reports actionable mismatches.
- [x] A real Release install tree is compared to the independent contract.
- [x] Every promised file is removed from genuine package output in regression
  coverage and every removal is rejected.
- [x] Every shipped executable is made non-executable in genuine package output
  and every mutation is rejected.
- [x] macOS release archives contain no AppleDouble metadata entries.
- [x] Two fresh candidates are byte-reproducible and independently verified.
- [x] Documentation matches the implemented package contract.
- [x] Existing Phase 1-9 and prior Phase 10 gates remain green in available
  environments.
- [x] No prohibited publication or external mutation occurred.

## 9. Recommended next work

Run independent Phase 10 verification against this remediation. Phase 10 is the
last defined implementation phase; do not invent Phase 11. If and only if the
verifier returns `ACCEPTED`, configure/confirm the documented repository and
package permissions and conduct a controlled first-release rehearsal in a
disposable fork/registry namespace before publishing a production version.
