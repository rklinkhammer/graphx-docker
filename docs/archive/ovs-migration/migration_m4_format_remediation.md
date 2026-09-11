# Migration M4 formatting remediation

**Remediated:** 2026-09-09 (America/New_York)  
**Prior verdict:** `migration_m4_reverification.md` — CHANGES REQUIRED  
**Prior report SHA-256:** `4b00edb6d254548704dc194867eab918b6e02d0d1331ce7289b93a2e1db86467`

## Scope

This remediation addresses only M4-001 from the independent re-verification:
the pinned LLVM 21 formatting failure. It does not change M4 behavior, expand
the accepted scope, or advance to M5.

The repository-pinned formatter was applied to exactly the three files named by
the finding:

- `src/config.cpp`
- `src/migration.cpp`
- `src/ownership.cpp`

The independent re-verification report is preserved unchanged at the SHA-256
recorded above.

## Remediation evidence

- The pinned LLVM 21 formatting check passes all 49 repository C++ files.
- The macOS development profile builds and passes all 40 tests.
- The quality profile completes clang-tidy and cppcheck and passes all 40 tests.
- The sanitizer profile passes all 40 enabled tests plus its coverage gate; its
  package test remains intentionally disabled by that profile.
- All five frozen M0 version-1 dry-run SHA-256 values remain exact.
- ARM64 Lima passes the M4 live container-veth regression and retained M3 live
  ownership regression with system OVS and rootful Docker.
- The disposable M4 containers, OVS bridge, host link, and test image are absent
  after cleanup.
- `git diff --check` passes.

## Acceptance status

The implementer-side formatting remediation and affected regression checks are
complete. M4 remains unaccepted until a new independent re-verification confirms
the formatting gate and the required M4 acceptance matrix.
