# ADR 0005: Portable quality gates and bounded fuzzing

## Status

Accepted for Phase 4.

## Context

GraphX supports C++20 and C++23, runs on Linux and macOS, parses untrusted
configuration and wire bytes, and owns several cancellation-sensitive transport
implementations. Before Phase 4, these behaviors had broad local coverage but no
checked-in CI policy, no continuously runnable fuzz entry point, and no
repository-wide static-analysis or formatting gate.

The quality layer must not change GraphX's deployment-neutral architecture or
silently weaken the Phase 3 wire contract. Privileged macvlan, ipvlan, OVS,
namespace, nftables, and netem tests also cannot safely run on ordinary shared CI
runners.

## Decision

1. GitHub Actions is the checked-in CI projection. External actions are pinned
   to immutable commit SHAs and workflow permissions are read-only.
2. Native builds test C++20 and C++23 on both Ubuntu and macOS. ASan and UBSan
   instrument every GraphX-owned library, application, test, and enabled fuzzer
   translation unit on both systems; Linux also requires leak detection. A
   compile-database CTest prevents sanitizer linkage without compilation
   instrumentation.
3. `.clang-format` is enforced across every tracked C++ source/header.
   `.clang-tidy` selects correctness, lifetime, performance, and portability
   checks. cppcheck supplies an independent warning/portability analysis. Both
   tools are wired through CMake only for GraphX-owned targets, excluding fetched
   dependencies.
4. Clang libFuzzer targets accept raw envelope bytes and framed-stream bytes.
   They are built only when explicitly enabled and always with ASan/UBSan.
   Golden v1/v2 fixtures seed a temporary corpus; routine CI campaigns are time
   bounded while local or scheduled campaigns can run longer.
5. Deterministic CTest regressions retain exact boundaries, golden compatibility,
   mixed-version transport behavior, and repeated connected transport
   delivery/reconnect/close/cancellation/resource reuse. Fuzzing supplements
   rather than replaces those assertions.
6. Privileged network laboratories remain explicit native-Linux tests. Portable
   CI validates their models and plans but never changes host networking.

## Consequences

- A pull request receives fast feedback across language standards and operating
  systems, plus independent dynamic and static analysis.
- Sanitizer and fuzz options are absent from production builds unless explicitly
  enabled.
- The complete repository now follows one formatting policy, which creates a
  one-time formatting-only diff in Phase 4.
- The bounded CI fuzz run proves harness health and catches shallow regressions;
  it is not evidence of exhaustive input coverage. Longer campaigns and corpus
  retention remain operational follow-up work.
- Host-specific privileged networking remains outside ordinary CI and must still
  be certified on native Linux before releases that change that layer.

## Rejected alternatives

- **Run only Linux:** rejected because socket, signal, sanitizer, and shared-memory
  behavior differs on macOS.
- **Use only sanitizer tests:** rejected because sanitizers do not explore parser
  state space without generated inputs.
- **Treat fuzzing as an unbounded pull-request job:** rejected because it makes
  completion nondeterministic. Pull requests use fixed-duration smoke campaigns.
- **Analyze fetched yaml-cpp sources as GraphX code:** rejected because upstream
  diagnostics would make local policy dependent on third-party internals.
- **Automatically run privileged network laboratories:** rejected because shared
  runners are not an appropriate or driver-accurate environment for host network
  mutation.
