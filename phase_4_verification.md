# Phase 4 independent verification report

Date: 2026-09-01  
Repository: `/Users/rklinkhammer/workspace/graphx-docker`  
Work package: Phase 4 — CI, sanitizers, fuzzing, static analysis, and expanded transport tests  
Baseline: accepted Phase 3 implementation recorded in `phase_3_verification.md`  
Implementation handoff: `phase_4_handoff.md`

## 1. Verdict

**ACCEPTED**

## 2. Executive summary

This is a fresh independent verification of the remediated Phase 4 workspace.
The two P2 findings and one P3 limitation from the previous verification have
been addressed effectively:

1. ASan/UBSan compilation now covers every GraphX-owned library, application,
   test, and enabled fuzzer translation unit. A compile-database audit runs in
   sanitizer CTest and in the fuzz runner. The verifier proved its negative path
   by removing instrumentation from one application command and observing the
   expected failure naming that source.
2. The proper-v2-prefix regression can no longer catch its own failure
   assertion. Its helper asserts normal return after the decoder exception
   handler, and a checked-in negative control proves that a no-op callable fails.
3. Lifecycle stress now performs connected delivery for all four transports,
   reuses shared-memory and Unix-domain names with cleanup assertions, accepts
   two sequential TCP peers per listener, and still proves blocked-receive
   cancellation.

Fresh C++20 and C++23 builds passed all 11 ordinary CTests. A clean macOS
ASan/UBSan build passed all 12 sanitizer CTests. A read-only Ubuntu 24.04
rehearsal passed clang-format 18, clang-tidy 18, cppcheck, and all 12 Clang 18
ASan/UBSan tests with LeakSanitizer enabled. Fresh bounded envelope and frame
libFuzzer campaigns completed on both local LLVM and Ubuntu Clang 18 without a
sanitizer, timeout, or invariant finding. The portable feature suite, production
dependency audits, Compose validation and image builds, and a live four-service
runtime smoke all passed.

No P0, P1, or P2 finding remains. One P3 maintainability/documentation issue is
recorded: `GRAPHX_BUILD_DIR` isolates the portable C++23 build but not the C++20
build, which continues to use a fixed directory. This does not invalidate Phase
4 because clean independent builds for both standards passed and CI starts from
a clean workspace, but the handoff's “fresh custom-directory” wording overstates
the isolation provided by that command.

## 3. Acceptance-criteria matrix

| Criterion | Result | Independent evidence |
|---|---|---|
| Checked-in CI for pull requests, `main`, schedule, and manual dispatch | PASS | `.github/workflows/ci.yml`; actionlint 1.7.7 returned success |
| Linux/macOS crossed with C++20/C++23 | PASS (definition); remote run unverified | Four-entry native matrix is present; fresh local C++20 and C++23 builds passed; Linux production and sanitizer builds passed |
| Immutable external actions and least-privilege workflow | PASS | `git ls-remote` confirmed checkout and setup-node v7.0.0 tag SHAs; workflow has `contents: read`, disables persisted credentials, and has no `pull_request_target` or privileged network mutation |
| ASan/UBSan on supported hosts and Linux leak detection | PASS | Fresh macOS 12/12 sanitizer CTests; fresh Ubuntu 24.04/Clang 18 12/12 with `detect_leaks=1` |
| Every GraphX-owned sanitizer target is compiled with instrumentation | PASS | Sanitized compile database contains 12 library, 4 application, and 2 test translation units with ASan/UBSan and frame pointers; fresh fuzz database contains both fuzzer units with the same flags plus libFuzzer |
| Sanitizer coverage regression detects missing instrumentation | PASS | Verifier-mutated database with flags removed from `apps/generator/main.cpp` failed and named that source |
| Sanitizers remain opt-in and absent from ordinary builds | PASS | `GRAPHX_ENABLE_SANITIZERS` defaults OFF; all 18 ordinary owned compile commands contain no sanitizer/frame-pointer policy flags |
| Repository formatting gate pinned to clang-format 18 | PASS | Ubuntu clang-format 18 checked all 36 C++ files; Homebrew clang-format 23 was rejected with the required-major diagnostic |
| clang-tidy and cppcheck cover GraphX production, application, and test targets | PASS | Fresh Ubuntu 24.04 quality build passed both analyzers while fetched yaml-cpp remained outside project policy |
| Envelope and frame libFuzzer targets under ASan/UBSan | PASS | Fresh local LLVM campaigns and Ubuntu Clang 18 campaigns completed; fuzz compile audit included both harnesses |
| Fuzzer configuration rejects unsupported combinations | PASS | Fuzzers enabled without sanitizers failed configuration with the documented requirement |
| CI fuzz durations and input/resource limits are bounded | PASS | Ordinary/scheduled durations are 30/300 seconds per target; maximum input is 1 MiB; per-input timeout is five seconds; workflow timeout is 15 minutes |
| Exact maximum and maximum-plus-one envelope/frame tests | PASS | Focused sanitized boundary test passed five consecutive repetitions; complete suites also passed |
| Every proper v2 prefix has an effective deterministic rejection regression | PASS | `expect_failure` asserts after its catch; checked-in no-op negative control passed; all 92 proper prefixes are exercised; five focused sanitized repetitions passed |
| One live TCP connection accepts v1 followed by v2 without identity drift | PASS | Focused sanitized test passed 25 consecutive repetitions; exact v1 bytes and v2 identities are asserted |
| Repeated connected lifecycle/cleanup/cancellation coverage for all four transports | PASS | 25 outer repetitions passed; each contains 12 connected cycles per transport, same-name shared-memory/Unix reuse, two TCP peers per listener, and blocked-receive cancellation |
| Test-runner filtering and repetition fail safely | PASS | Empty selection and repeat zero both exited 2 with actionable diagnostics |
| Portable configuration, runtime, web, telemetry, capture, and control acceptance | PASS | Fresh custom C++23 build; both standards' 11/11 CTests; topology/dry-run plans; TCP/shared-memory pipelines; SIGTERM; web/telemetry/API/control/capture/extcap checks all passed |
| Production dependency audit | PASS | Telemetry and web `npm audit --omit=dev`: zero vulnerabilities each |
| Compose validation, affected builds, and standard runtime smoke | PASS | Compose model accepted; images rebuilt; four services ran; both edges connected and advanced; API/Prometheus live; cleanup left the project empty |
| Privileged native networking excluded from ordinary CI | PASS | Workflow contains no macvlan/ipvlan/OVS/netns/nftables/netem mutation or privileged opt-in |
| Phase 3 wire/config/API invariants preserved | PASS | Golden fixture SHA-1 values remain `a4d1ccf...` and `134c829...`; configuration/schema/examples have no Phase 4 diff; complete and mixed-version suites passed |
| Documentation and ADR describe actual behavior | PASS with P3 note | Quality policy, commands, exclusions, limits, and deferred privileged tests match behavior; custom-directory freshness wording is imprecise for C++20 |
| No premature Phase 5/6 implementation | PASS | Phase 4 changes remain quality/test focused, apart from the scoped best-effort exporter RAII/exception-containment correction found by analysis |

## 4. Findings

### P3-1 — Portable build-directory override does not isolate the C++20 build

- **Component/location:** `scripts/test-features.sh:6,40-54` and
  `phase_4_handoff.md:216`.
- **Evidence/reproduction:** Running
  `GRAPHX_BUILD_DIR="$PWD/build/verify-phase4-remediated-portable" scripts/test-features.sh portable`
  configured C++23 in the requested directory, but the C++20 step reported
  `build/cxx20-features` and `ninja: no work to do`. The script hard-codes that
  path at lines 51-54.
- **Expected:** A custom acceptance build root should isolate both supported
  standard configurations, or the documentation should state that the override
  applies only to the primary C++23 build.
- **Actual:** C++20 reuses a shared fixed directory, so a nominal
  “fresh custom-directory” portable run can consume an existing C++20 cache and
  concurrent custom runs can collide there.
- **Impact:** Local evidence can look fresher and more isolated than it is. This
  is non-blocking because the verifier separately performed a genuinely clean
  C++20 build and CI uses a clean workspace.
- **Specific remediation:** Derive separate C++23 and C++20 directories from a
  common configurable build root (or add `GRAPHX_CXX20_BUILD_DIR`) and correct
  the handoff wording.
- **Missing regression test:** Exercise the portable script with a temporary
  build root and assert that both generated CMake caches reside beneath it.

No P0, P1, or P2 finding was identified.

## 5. Tests and checks run

All repository commands ran from `/Users/rklinkhammer/workspace/graphx-docker`.

| Check | Exact verifier result |
|---|---|
| `git diff --check` | PASS |
| Fresh macOS C++20 configure/build/CTest (`build/verify-phase4-remediated-cxx20`) | PASS; 11/11, 5.30 seconds |
| Fresh macOS C++23 configure/build/CTest (`build/verify-phase4-remediated-cxx23`) | PASS; 11/11, 4.79 seconds |
| Fresh macOS AppleClang ASan/UBSan (`build/verify-phase4-remediated-sanitizers`) | PASS; 12/12, 9.49 seconds; leak detection disabled because Apple runtime support is not assumed |
| Fresh Ubuntu 24.04/Clang 18 ASan/UBSan + LeakSanitizer | PASS; 12/12, 6.76 seconds |
| Sanitizer compile-command audit | PASS; 18/18 ordinary owned translation units instrumented in sanitizer build; 0/18 carry sanitizer policy flags in ordinary build |
| Sanitizer-audit negative control | PASS; deliberately de-instrumented generator command rejected and identified |
| Fresh local libFuzzer runner with Homebrew Clang 23 | PASS; audit found 12 library, 4 application, and 2 fuzzer units; envelope 300,395 runs and frame 432,243 runs; no findings |
| Fresh Ubuntu 24.04/Clang 18 libFuzzer runner | PASS; both 3-second targets and the fuzzer compile audit completed before the verifier's subsequent optional display command failed because `rg` was not installed in the minimal container |
| Fuzz-without-sanitizers guard | PASS; configure rejected the combination |
| Ubuntu clang-format 18 | PASS; 36 C++ files |
| Wrong formatter-major rejection | PASS; clang-format 23 rejected before file checking |
| Ubuntu clang-tidy 18 + cppcheck | PASS over library, applications, and tests |
| Focused protocol boundaries under sanitizers | PASS; 5 repetitions |
| Focused mixed v1/v2 TCP under sanitizers | PASS; 25 repetitions |
| Connected lifecycle stress under sanitizers | PASS; 25 repetitions × 12 internal cycles for every transport |
| Test-runner invalid filter and repeat zero | PASS; each exited 2 with the expected diagnostic |
| Fresh custom-directory portable feature suite | PASS overall; C++23 was fresh; C++20 passed 11/11 but reused `build/cxx20-features`; all topology/runtime/web/telemetry/control/capture checks passed |
| Web production build | PASS; 1,750 modules; existing approximately 1.846 MB chunk warning |
| Web/telemetry unit and lint scripts | NOT AVAILABLE; package manifests define no test or lint commands |
| Telemetry production npm audit | PASS; zero vulnerabilities |
| Web production npm audit | PASS; zero vulnerabilities |
| `docker compose -f compose.yaml config --quiet` | PASS |
| `docker compose -f compose.yaml build` | PASS; runtime and telemetry images rebuilt |
| Live `scripts/demo.sh start/verify/stop` | PASS; 4/4 services, both edges connected, counters advanced 1→4 and 5→9, API and Prometheus live |
| Compose cleanup | PASS; standard project empty; three pre-existing mixed-network containers remained running and untouched |
| actionlint 1.7.7 | PASS |
| Action tag SHA verification | PASS; checkout/setup-node hashes equal their upstream v7.0.0 tag refs |
| Shell syntax and CMake preset JSON parse | PASS |
| Golden fixture/configuration drift check | PASS; fixture hashes unchanged; no configuration/schema/example diff |

The Ubuntu command's final status was 127 only because the verifier attempted to
summarize the already-successful fuzz log with `rg`, which was not among the
installed container tools. With `set -e`, reaching that final command proves the
preceding formatter, analysis, sanitizer, CTest, coverage-audit, and fuzz commands
all returned success. This wrapper diagnostic is not a GraphX failure.

## 6. Unverified areas and why

1. **GitHub-hosted execution:** The workflow remains uncommitted, so no remote
   Actions run exists. Its syntax, action SHAs, permissions, Ubuntu commands,
   local macOS behavior, and representative Linux behavior were verified.
2. **Exact hosted macOS 15 image:** Local verification used macOS 26.6.2 arm64
   with AppleClang 21 rather than GitHub's `macos-15` image. Only a future remote
   run can certify that exact runner image.
3. **Full scheduled fuzz duration:** Fresh three-second campaigns establish
   harness health but cannot prove parser defect absence or reproduce the weekly
   300-second runtime.
4. **Privileged native Linux networking:** macvlan, ipvlan, OVS, namespaces,
   nftables, and netem were not executed. The verifier host is macOS/Docker
   Desktop, which cannot certify native driver behavior. Phase 4 does not change
   that layer, and all checked-in models/dry-run plans passed.
5. **Web unit/lint suite:** No corresponding package scripts exist. The browser
   code is unchanged by Phase 4; its production build and portable end-to-end
   behavior passed.

These limitations do not make verification blocked because Phase 4's changed
quality gates and regressions were independently executable locally and in a
clean Ubuntu environment.

## 7. Compatibility and security assessment

### Compatibility

- Exact v1/v2 fixture hashes are unchanged, and complete protocol tests passed
  in C++20, C++23, macOS sanitizer, and Linux sanitizer configurations.
- Configuration schema, authoritative topology, and example configurations have
  no Phase 4 diff. Every checked-in topology validated and produced dry-run
  infrastructure plans.
- Mixed v1/v2 traffic preserved exact v1 bytes and v2 message/trace identity on
  one TCP connection for 25 verifier repetitions.
- Sanitizer, analysis, and fuzzer options default OFF and do not alter release
  wire bytes, configuration semantics, or public runtime interfaces.
- The best-effort OTLP sender's RAII and outer exception boundary preserve
  telemetry isolation; inspection, static analysis, sanitizer tests, and
  portable telemetry behavior found no regression.

### Security and operations

- Workflow permissions are read-only, credentials are not persisted, external
  actions are SHA-pinned, duplicate runs are cancelled, and every job has a
  deadline.
- Pull-request execution has no privileged networking path or
  `pull_request_target` exposure.
- Fuzz input length, per-input time, campaign duration, and CI job duration are
  bounded. Malformed protocol input remains expected while sanitizer and
  invariant failures remain fatal.
- Production dependency audits reported zero known vulnerabilities.
- Runtime images use non-root users. Authentication, TLS, API hardening,
  healthchecks, and broader container hardening remain explicitly assigned to
  Phase 5 and are not falsely presented as Phase 4 protections.
- No new secrets, payload logging, deployment coupling, or privileged runtime
  behavior was introduced by Phase 4.

## 8. Required remediation before acceptance

None. There is no P1/P2 issue or unmet Phase 4 acceptance criterion.

P3-1 should be corrected opportunistically so custom portable runs isolate both
language-standard builds and the handoff describes their freshness precisely.

## 9. Readiness for the next work package

The repository is ready to proceed to **Phase 5: authentication, TLS, API
validation, and container hardening**.

Phase 5 must preserve exact v1/v2 bytes, the 16 MiB and attribute limits, stable
message/trace identity, fork-safe span IDs, typed timeout/cancellation/end-of-
stream results, bounded queues and telemetry exporters, best-effort telemetry
isolation, transport/deployment neutrality, non-root runtime behavior, and every
Phase 4 CI/sanitizer/static-analysis/fuzz/compatibility gate. Security failures
should become deterministic negative tests and appropriate fuzz seeds, while
sanitizer and fuzz runtimes must remain absent from production images.
