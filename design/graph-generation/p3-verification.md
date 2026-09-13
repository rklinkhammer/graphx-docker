# P3 implementation verification

P3 implements deterministic artifact compilation in the C++ library and the thin
`graphx compile` CLI. It does not start containers, native applications, guests,
credential providers or infrastructure commands. I-01–I-11 remain accepted.

The release-pin dependency is resolved by sequencing: P3 preserves catalog
identities and marks them **unverified**, P4 supplies verified runtime/platform
images and release packaging, and P8 supplies verified guest artifacts. This is
the continuation agreed after the P3 dependency discussion. No illustrative digest
has been relabeled as an actual built artifact. Both the compile manifest and
execution plan explicitly disable execution; `graphx run` and `graphx infra`
remain gated. P3 completion is compiler acceptance, not deployment acceptance.

## Implemented contract

- `compile_graph` consumes the loader's resolved graph and verified catalog
  snapshot. The loader retains catalog content without reopening it during pure
  compilation. Duplicate image/executable fields were removed from the old type
  projection; the compiler uses the resolved catalog definitions directly.
- Artifacts include canonical node files, normalized graph, platform settings,
  scoped credential descriptors, catalog pins, substitution declarations and a
  provenance manifest. Compose, native process, OVS, capture handoff, QEMU,
  guest-build and scenario plans are emitted when applicable. No Dockerfiles,
  shell launchers, secrets, timestamps or runtime-generated identities are emitted.
- Node objects are serialized exactly from normalized nodes. Fixed node-v1 and
  platform-v1 contracts select argv and service isolation; no arbitrary template
  interpreter is introduced. P2 node commands include mandatory identity/config
  and release-file/token arguments. Readiness is owned stdout evidence, not an
  unsupported health-probe CLI flag. Scoped HMAC file references are explicit in
  launch environments; P5 still provisions credentials and integrates the platform/SDR
  credential readers. Namespace preparation precedes namespace process launch,
  through the existing resource lifecycle rather than a second ownership store.
- JSON uses sorted keys, two spaces, LF and a terminal newline. `compose.yaml`
  uses the same JSON serializer: JSON is a YAML 1.2 subset with quoted scalars and
  no aliases. The compiler/serializer versions are recorded in the manifest.
  Semantically ordered actions and argv remain ordered. Resource names reuse
  the loader's common deterministic naming function.
- The only execution substitutions are `GX_OUTPUT`, `GX_STATE`, `GX_CREDENTIALS`,
  `GX_RELEASE` and `GX_OWNER`. Actual root locations, UIDs, staging names and
  environment variables never affect artifact bytes. Guest source/output identities
  remain explicit catalog data; missing future builds are not executed or guessed.
- Publication validates all artifact paths, manifest hashes and the 32 MiB package
  bound before staging. It uses private sibling staging, fsync and an exclusive
  same-filesystem rename. Parent traversal and symlink components fail closed;
  output must be outside input, catalog, source and credential roots. Every
  existing destination is refused, including empty directories and valid prior
  compilations. Replacement needs P6 ownership/inactivity evidence that P3 cannot
  establish, so `--replace` is deliberately unavailable. In-process write failures
  clean only identity-checked staging. An externally killed compiler may leave a
  private staging directory for inspection, but never publishes a partial package.

## Evidence

Verification completed on 2026-09-13 on native macOS ARM64:

| Check | Result | Evidence |
|---|---|---|
| `PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quick` | 32/32 CTest checks passed | `outputs/verification/20260913T155945Z-quick.log` |
| `PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quality` | Formatting, clang-tidy and cppcheck passed | `outputs/verification/20260913T161048Z-quality.log` |
| `PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh portable` | 32/32 CTest checks, 32 execution gates, 92 telemetry tests, 17 web tests and console production build passed | `outputs/verification/20260913T161121Z-portable.log` |
| `GRAPHX_COMPOSE_VALIDATE=1 python3 tests/test_compile.py build/dev/graphx .` | 63 supported packages repeat byte-for-byte; 15 negative cases, 33 unsupported targets and 51 read-only Compose configurations passed | Compiler matrix and maintained goldens |
| `ctest --test-dir build/dev --output-on-failure -R '^graphx-package$'` | Release package check passed, including the public compiler header | CTest package check |
| `git diff --check` | Passed | Working-tree whitespace check |

The final portable run includes the full development CTest suite after the final
compiler changes. Golden generation used the same matrix command with
`GRAPHX_UPDATE_COMPILE_GOLDENS=1`; ordinary verification does not update fixtures.
No shell scripts changed, so ShellCheck is not applicable.

`tests/test_compile.py` covers all 63 supported design case/target combinations,
15 negative inputs and 33 unsupported targets. Each successful compilation is
repeated with different ambient environment values and compared byte-for-byte.
Five maintained golden packages under `tests/fixtures/compiled/` cover all
serializer families using production catalog inputs. The design review's expected
artifacts remain illustrative; they are not substituted for compiler test results.

Additional checks cover renamed/relocated input roots, shuffled YAML/JSON maps,
node-file equivalence, manifest hashes, credential mount scope, release ordering,
OVS intent preservation, implicit-action refusal, symlinked inputs/parents/outputs,
overlapping roots, catalog tampering, repinned hostile templates, existing-output
preservation and concurrent publishers. Runtime command shims fail if the compiler
attempts to invoke Docker, QEMU, networking tools or credential tooling.

All tests are unprivileged native macOS ARM64 checks. Docker Compose configuration
validation is read-only and does not contact/start an engine.
No Linux/Lima/OrbStack workload execution, OVS/veth/TAP mutation, guest build,
TCG boot or KVM evidence is claimed. P4–P9 remain execution/packaging gates.
