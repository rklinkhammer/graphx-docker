# Phase 4 implementation handoff

Date: 2026-09-01  
Repository: `/Users/rklinkhammer/workspace/graphx-docker`  
Work package: CI, sanitizers, fuzzing, static analysis, and expanded transport tests

## 1. Outcome summary

Phase 4 is implemented. GraphX now has a checked-in, read-only GitHub Actions
pipeline; Linux/macOS and C++20/C++23 build matrices; explicit ASan/UBSan CMake
support; repository-pinned clang-format 18, clang-tidy, and cppcheck gates; two
coverage-guided libFuzzer targets; deterministic protocol boundary and
mixed-version regressions; and repeated lifecycle/cancellation coverage for all
four transports.

The 2026-09-01 remediation addresses every finding in
`phase_4_verification.md`. Sanitizer compile flags now apply to every
GraphX-owned library, application, test, and enabled fuzzer translation unit; a
new compile-database CTest proves that coverage and rejects the prior incomplete
configuration. The exhaustive prefix regression now uses a failure helper whose
success assertion is outside its exception handler. Lifecycle stress now adds
real connected delivery, reusable shared-memory and Unix-domain names, and TCP
peer replacement in addition to repeated cancellation.

The quality gates found and corrected one production issue: the best-effort
OTLP HTTP sender was declared `noexcept` but could terminate on an allocation
failure and could leave its descriptor open on an exceptional path. Address
resolution and the socket are now scoped resources, and every exporter-side
exception is contained as required by the best-effort telemetry contract.

No envelope bytes, framing bytes, configuration semantics, transport public
interfaces, identity semantics, deployment model, or dependency versions changed.
Phase 5 authentication/TLS/API/container hardening and Phase 6 tracing expansion
were not implemented.

## 2. Requirements and acceptance criteria defined before implementation

### Concrete requirements

1. Run C++20 and C++23 clean builds and complete tests on Linux and macOS.
2. Run AddressSanitizer and UndefinedBehaviorSanitizer on both supported hosts,
   with LeakSanitizer required where the platform supports it.
3. Enforce one reproducible formatting policy and independent clang-tidy and
   cppcheck analysis over GraphX-owned production, application, and test targets.
4. Provide real libFuzzer entry points for raw envelope and framed-stream input,
   built with ASan/UBSan and seeded from the Phase 3 golden vectors.
5. Add deterministic tests for exact limits, all proper truncated v2 prefixes,
   mixed v1/v2 traffic, and repeated transport startup/close/cancellation.
6. Keep ordinary CI unprivileged; validate network models and plans without
   mutating runner networking.
7. Keep local commands, CI commands, documentation, and CMake behavior aligned.

### Preserved invariants

- Exact v1 and v2 golden bytes and deterministic attribute order.
- The 16,777,216-byte envelope/frame limit and 4,096-attribute limit.
- Strict rejection of malformed, truncated, duplicate, trailing, and
  unknown-version data.
- Stable logical message/trace identity and explicit derived-message lineage.
- Fork-safe, non-zero, distinct OTLP span identity.
- Failed in-process sends have no queue side effects.
- Queue bounds and distinct message/timeout/end-of-stream/cancellation outcomes.
- Core GraphX semantics remain independent of Docker, CI, GUI, and telemetry
  vendors.

### Failure cases and resource limits

- CI jobs have explicit 15–30 minute deadlines and cancel superseded branch runs.
- Pull-request fuzzing is 30 seconds per target; weekly fuzzing is 300 seconds per
  target; inputs are capped at 1 MiB. Exact 16 MiB behavior is deterministic CTest
  coverage rather than a routine fuzz allocation.
- Fuzzers are rejected at configure time unless Clang/AppleClang and sanitizers
  are enabled.
- Analysis fails configuration if clang-tidy or cppcheck is unavailable.
- Formatting fails if the tool is not clang-format 18.x, preventing output drift
  between formatter releases.
- Privileged Linux network tests remain manual and require an explicit opt-in.

## 3. Requirements implemented

### Continuous integration

`.github/workflows/ci.yml` contains six jobs:

1. `native`: Ubuntu 24.04 and macOS 15 crossed with C++20 and C++23.
2. `sanitizers`: ASan/UBSan on both hosts, Linux leak detection enabled.
3. `quality`: clang-format 18, clang-tidy 18, and cppcheck on Ubuntu.
4. `fuzz`: bounded Clang 18 libFuzzer campaigns.
5. `portable`: the full portable acceptance suite plus npm production audits.
6. `containers`: Compose model validation and affected image builds.

The workflow runs for pull requests, pushes to `main`, manual dispatch, and a
weekly scheduled campaign. `actions/checkout` and `actions/setup-node` are pinned
to immutable v7.0.0 commit SHAs. Permissions are `contents: read`, checkout does
not retain credentials, and concurrency cancels superseded runs.

The matrix structure follows the
[GitHub Actions matrix guidance](https://docs.github.com/en/actions/how-tos/write-workflows/choose-what-workflows-do/run-job-variations).

### Sanitizers and analysis

`GRAPHX_ENABLE_SANITIZERS` adds `-fsanitize=address,undefined` and frame pointers
to every GraphX-owned library, application, test, and enabled fuzzer translation
unit, as well as the corresponding executable link options. The
`graphx-sanitizer-coverage` CTest audits the generated compile database and fails
if any project-owned source lacks either compile flag; the fuzz runner invokes
the same audit after configuring its enabled fuzzer targets. Fetched yaml-cpp
remains excluded. Sanitizer runtimes are never enabled in a default/production build.
The sanitizer choices follow LLVM's documented
[AddressSanitizer build guidance](https://clang.llvm.org/docs/AddressSanitizer.html).

`GRAPHX_ENABLE_CLANG_TIDY` and `GRAPHX_ENABLE_CPPCHECK` attach analysis only to
GraphX-owned targets. Fetched yaml-cpp sources remain outside project policy.
`.clang-tidy` enables selected correctness, exception, lifetime, performance, and
portability checks and treats them as errors. cppcheck independently enforces
warning and portability diagnostics.

`scripts/check-format.sh` checks every tracked or newly added repository `.cpp`
and `.hpp` with the pinned
clang-format 18 major. Phase 4 includes the one-time mechanical normalization of
previously unformatted tracked C++ files. No semantic change is attributed to
those formatting-only edits.

### Fuzzing

- `graphx-envelope-fuzz` passes arbitrary bytes to the envelope decoder. Valid
  inputs must serialize, decode again, preserve every field, and remain bounded.
- `graphx-frame-fuzz` checks frame construction/length-prefix invariants and, when
  a complete declared payload is present, exercises envelope parsing and
  reserialization.
- `fuzz/envelope.dict` supplies protocol magic, versions, and important boundary
  tokens.
- `scripts/run-fuzz.sh` converts exact v1/v2 hexadecimal fixtures into a temporary
  binary corpus, configures the explicit fuzz build, and runs both targets with
  ASan/UBSan, per-input timeout, input bound, and campaign deadline.

Malformed inputs are expected and caught by the harness. Sanitizer failures and
explicit invariant violations still terminate the fuzz target.

### Expanded deterministic tests

- Every proper prefix of the 92-byte v2 golden envelope is rejected as truncated;
  normal decoder return fails outside the exception handler and cannot be
  mistaken for the expected parse error.
- An exact 16 MiB envelope and frame are accepted and round-trip; maximum plus one
  is rejected.
- One real TCP connection receives a byte-stable v1 envelope followed by a v2
  envelope with its canonical identities intact.
- In-process, shared-memory, TCP, and Unix-domain transports repeatedly deliver
  real envelopes and cancel a blocked receive on close. Shared-memory objects
  and Unix socket paths reuse one name across iterations to prove cleanup, and
  TCP accepts two sequential peers per listener before cancellation.
- `graphx-tests` now supports `--filter` and `--repeat`, with invalid arguments and
  empty selections reported as test-runner errors.
- CTest exposes focused boundary, mixed-version, and lifecycle-stress entries in
  addition to the complete suite.

## 4. Architecture and compatibility decisions

ADR 0005 records the quality architecture. CI is a projection of repository
commands rather than a new GraphX runtime dependency. Sanitizer, analysis, and
fuzz flags are explicit opt-ins, so release binaries remain unchanged.

Deterministic regressions remain authoritative for exact compatibility and
limits. Fuzzing supplements them by exploring parser state space; it does not
replace golden assertions or claim exhaustive coverage. The routine input cap
keeps CI resource use bounded while exact maximum allocation behavior stays in a
stable test.

Privileged host networking is deliberately excluded from shared CI. Portable
validation still checks every topology and dry-run infrastructure plan. Native
macvlan/ipvlan/OVS/netns/nftables/netem certification retains the explicit Linux
opt-in boundary.

## 5. Files and major components changed

| Area | Files | Purpose |
|---|---|---|
| CI | `.github/workflows/ci.yml` | Cross-platform builds, sanitizers, quality, fuzz, portable, audits, containers |
| CMake | `CMakeLists.txt`, `CMakePresets.json`, `cmake/verify-sanitizer-coverage.cmake` | Opt-in gates, presets, all-target sanitizer instrumentation, compile-database regression, fuzzer targets, focused CTests |
| Quality policy | `.clang-tidy`, `.clang-format`, `scripts/check-format.sh`, `scripts/run-static-analysis.sh` | Pinned, locally reproducible analysis |
| Fuzzing | `fuzz/envelope_fuzz.cpp`, `fuzz/frame_fuzz.cpp`, `fuzz/envelope.dict`, `scripts/run-fuzz.sh` | Raw protocol fuzz entry points and bounded runner |
| Tests | `tests/test_main.cpp` | Effective exhaustive boundaries, mixed versions, connected lifecycle/reuse stress, filter/repeat runner |
| Acceptance script | `scripts/test-features.sh` | Correctly honors a custom `GRAPHX_BUILD_DIR` for a fresh portable build |
| Production resilience | `src/observability.cpp` | Exception containment and RAII cleanup in best-effort OTLP HTTP delivery |
| Documentation | `README.md`, `docs/test-procedure.md`, ADR 0005 | Commands, CI tiers, policy, limits, rationale |
| Work-package metadata | `prompt/implement.md`, `prompt/verifier.md` | Phase 4 selection and handoff path |
| Formatting-only normalization | tracked C++ files reported by the diff outside the functional files above | Repository-wide clang-format 18 baseline |

`src/config.cpp` also changes one local YAML scalar binding from a copy to a
reference after clang-tidy identified the unnecessary copy. The node owns the
referenced scalar for the full function call, and the returned configuration
value remains a copy; there is no lifetime or behavior change.

## 6. Tests and checks run

All repository commands ran from `/Users/rklinkhammer/workspace/graphx-docker`.

| Check | Result |
|---|---|
| Fresh C++23 Debug build, `build/phase4-remediation-cxx23` | PASS |
| Complete fresh C++23 CTest | PASS; 11/11, 0 failures, 4.98 s |
| Fresh C++20 Debug build, `build/phase4-remediation-cxx20` | PASS |
| Complete fresh C++20 CTest | PASS; 11/11, 0 failures, 5.03 s |
| Fresh Darwin ASan/UBSan build, `build/phase4-remediation-sanitizers` | PASS |
| Complete Darwin ASan/UBSan CTest | PASS; 12/12, 0 findings, 9.49 s, including the sanitizer-coverage audit; leak detection disabled because this Apple runtime does not support it reliably |
| Sanitizer compile-database audit | PASS; all 18 GraphX-owned `src`, `apps`, and `tests` translation units carry ASan/UBSan and frame-pointer flags; the audit rejects the verifier's prior incomplete compile database |
| Fuzzer compile-database audit | PASS; both enabled fuzzer translation units carry ASan/UBSan, frame-pointer, and libFuzzer flags; the fuzz runner now enforces this before each campaign |
| Repeated sanitizer lifecycle target | PASS; 10 runner repetitions × 12 internal iterations × 4 transports |
| Proper-prefix rejection-helper negative control | PASS; an intentionally successful callable is rejected, proving the helper cannot catch its own assertion |
| Ubuntu 24.04 / Clang 18 ASan+UBSan+LeakSanitizer container rehearsal | PASS; 12/12, 0 findings, 6.81 s, including the compile-database audit |
| Exact Ubuntu clang-tidy 18 + cppcheck container rehearsal | PASS over production, applications, and tests |
| Exact clang-format 18 repository gate | PASS; 36 tracked or newly added C++ files |
| 3-second Ubuntu Clang 18 libFuzzer rehearsal per target | PASS; no crashes, timeouts, invariant failures, ASan, or UBSan findings |
| Post-remediation complete local fuzz runner with Homebrew Clang 23 | PASS; compile-database audit plus 1-second campaign per target, no findings |
| Fresh custom-directory `scripts/test-features.sh portable` | PASS; both standards, 11/11 CTests, topology/infra plans, TCP/shared-memory processes, SIGTERM, telemetry/API/control, captures/extcap, web build |
| Telemetry npm production audit | PASS; 0 vulnerabilities |
| Web npm production audit | PASS; 0 vulnerabilities |
| Web production build | PASS; 1,750 modules; existing approximately 1.846 MB chunk warning |
| Workflow YAML parse, CMake preset JSON parse, shell syntax, `git diff --check` | PASS |
| Compose model validation and affected image builds | PASS; GNU C++ container build completed |
| Live four-service Compose smoke | PASS; 4/4 services, both edges connected, both counters advanced 1→4 and then 5→8, API and Prometheus live |
| Compose cleanup | PASS; `docker compose ps --all` empty |

The exact Ubuntu rehearsals use a read-only repository mount and temporary build
directories. They validate the workflow's clang-format 18, clang-tidy 18,
cppcheck, Clang 18 sanitizer, Linux LeakSanitizer, and libFuzzer assumptions
without publishing images or changing external systems.

## 7. Known limitations and verifier risks

1. **Remote workflow execution:** `.github/workflows/ci.yml` is uncommitted and
   therefore has not run on GitHub-hosted runners. Its YAML, commands, action
   SHAs, Ubuntu toolchain, and local macOS equivalents were validated, but the
   verifier must distinguish that evidence from a real Actions run.
2. **Native privileged networking:** macvlan, ipvlan, OVS, namespaces, nftables,
   and netem were not executed because the implementation host is macOS. Phase 4
   does not change that layer; model/dry-run validation and bridge Compose passed.
3. **Fuzz completeness:** time-bounded runs verify harness health and found no
   defect; they do not prove absence of parser bugs. The corpus is temporary and
   CI does not yet persist newly discovered inputs as artifacts.
4. **ThreadSanitizer/MemorySanitizer:** Phase 4 standardizes ASan, UBSan, and Linux
   LeakSanitizer. TSan and MSan are not configured; they require separate handling
   for fork/process-heavy integration tests and fully instrumented dependencies.
5. **Web bundle:** the existing large-chunk warning remains. Phase 4 does not
   change browser code and the production build succeeds.
6. **Best-effort allocation regression:** the OTLP `noexcept` defect is guarded by
   clang-tidy and code structure. Deterministically injecting allocator failure
   would require a new allocation seam that is disproportionate to this private
   helper.

Verifier focus should include workflow expression/action syntax, at least one
fresh run of each preset, deliberate failure of the sanitizer compile-database
audit when an owned target lacks instrumentation, the prefix-helper negative
control, the exact formatter major rejection path, fuzzer configure guards,
Linux leak detection, mixed v1/v2 identity preservation, connected TCP peer
replacement, and repeated shared-memory/Unix-name cleanup.

## 8. Acceptance-criteria checklist

- [x] Linux/macOS C++20/C++23 CI matrix is checked in.
- [x] External actions are immutable-SHA pinned and workflow permissions are read-only.
- [x] ASan/UBSan are opt-in, cross-platform, cover all GraphX-owned targets, and are tested by a compile-database regression.
- [x] Linux LeakSanitizer is enabled and independently rehearsed.
- [x] clang-format 18 is pinned and the entire repository C++ tree passes.
- [x] clang-tidy and cppcheck are CMake-integrated for GraphX-owned targets.
- [x] Envelope and frame coverage-guided fuzz targets are checked in and runnable.
- [x] Pull-request and weekly fuzz durations are bounded.
- [x] Golden v1/v2 compatibility and all Phase 3 invariants remain intact.
- [x] Exact maximum and maximum-plus-one protocol boundaries are deterministic tests.
- [x] Every proper v2 truncation is an effective deterministic rejection test, with a negative control for the failure helper.
- [x] A real TCP connection accepts v1 followed by v2 without identity drift.
- [x] All four transports have repeated connected delivery, close/cancellation, and cleanup/reuse stress coverage; TCP also exercises sequential peer replacement.
- [x] Portable, web, dependency, Compose build, and live runtime checks pass.
- [x] Documentation and ADR describe actual commands, limits, and deferred areas.
- [x] Privileged host networking remains explicit and is not falsely reported as verified.
- [x] No Phase 5 or Phase 6 subsystem was prematurely implemented.

## 9. Recommended next work package

Proceed to **Phase 5: authentication, TLS, API validation, and container
hardening** only after independent Phase 4 verification accepts this handoff.

Phase 5 must preserve the exact v1/v2 bytes, parser/resource limits, stable
message and trace identity, fork-safe span IDs, typed transport outcomes, bounded
queues/exporters, best-effort telemetry isolation, deployment-neutral core, and
all Phase 4 gates. Security failures should be added as deterministic negative
tests and fuzz seeds where applicable, but sanitizer/fuzz runtimes must never be
included in production images.
