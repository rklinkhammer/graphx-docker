# GraphX test procedure

This document owns automated verification profiles, prerequisites, and focused
reruns for developers and independent verifiers. Choose one profile, run one
command, and retain the generated log. Detailed
coverage, rationale, and manual diagnostics are in
[`test-reference.md`](test-reference.md). Platform-specific interactive
acceptance and cleanup steps are in
[`manual-test-procedures.md`](manual-test-procedures.md); runnable scenario
walkthroughs are in [`demo-guide.md`](demo-guide.md).

## Choose a profile

| Profile | Use it when | Coverage | Host |
|---|---|---|---|
| `quick` | Editing C++ code | Fresh development build and CTest | macOS or Linux |
| `quality` | Investigating a quality failure | Formatting, clang-tidy, and cppcheck | macOS or Linux |
| `sanitizers` | Investigating a sanitizer failure | Platform-safe LLVM 21 sanitizer build and CTest | macOS or Linux |
| `fuzz` | Investigating a fuzzer failure | Bounded LLVM 21 envelope and frame fuzzing | macOS or Linux |
| `portable` | Preparing a normal change | C++20/23, configurations, process pipelines, telemetry, web, and portable examples | macOS or Linux |
| `full` | Preparing a pull request | Formatting, static analysis, sanitizers, fuzzing, portable acceptance, and Docker acceptance | macOS with OrbStack, or Linux with Docker Engine |
| `native-linux` | Certifying native network behavior | Portable acceptance plus UDP broadcast, OVS semantic profiles, namespaces, nftables, netem, and live packet capture | Native Linux only |
| `release` | Building a release candidate | Clean release build, package contract, SBOM, checksums, and independent artifact verification | Supported release host |

`native-linux` is the only profile that certifies a native Linux host. The
underlying OVS lifecycle can also be verified inside Lima, but that result must
be reported separately as Lima ARM64 evidence.

## Prerequisites

All profiles need CMake 3.25+, Ninja, OpenSSL 3 development files, and a C++20/23
compiler. Portable testing also needs Node.js, npm, and curl.

Additional requirements:

- `full`: a reachable Docker engine and Docker Compose, LLVM/Clang 21 with its sanitizer and libFuzzer
  runtimes, clang-format 21, clang-tidy 21, cppcheck, and `xxd`. The executable names do not
  have to contain `-21`; use the overrides below when a package manager uses
  unversioned names or keeps LLVM 21 outside `PATH`.
- `native-linux`: Docker Compose, Open vSwitch, iproute2, nftables, dumpcap,
  tshark, and sudo access.
- `release`: Python 3 and a clean Git worktree at the intended release commit.

The commands stop at the first failure and write a combined log under
`outputs/verification/`. Set `GRAPHX_VERIFY_LOG_DIR` to use another location.
Portable tests isolate telemetry and web subprocesses from inherited `GRAPHX_*`
deployment variables, so container-only secret paths cannot affect host tests.
The configuration sweep validates and inspects every top-level example. It
generates infrastructure dry-runs only for version-2 configurations and asserts
that version-1 infrastructure execution is rejected before mutation.

### macOS LLVM 21 setup

Homebrew's `llvm@21` formula is keg-only. It supplies `clang-format` and
`clang-tidy` beneath its own prefix, not commands named `clang-format-21` and
`clang-tidy-21`. Install the pinned tools:

```sh
brew install llvm@21 cppcheck orbstack
open -a OrbStack
docker context use orbstack
docker info
docker compose version
scripts/verify.sh full
```

The `full` profile discovers Homebrew `llvm@21`, selects its compiler, formatter,
analyzer, symbolizer, and a host-compatible installed macOS SDK automatically.
If the active Command Line Tools SDK is newer than the running macOS release,
the profile selects the matching installed major-version SDK. To inspect the
installed versions independently:

```sh
"$(brew --prefix llvm@21)/bin/clang-format" --version
"$(brew --prefix llvm@21)/bin/clang-tidy" --version
/usr/bin/c++ --version
```

Do not point these variables at Homebrew's current unversioned `llvm` formula
unless it is LLVM 21. Formatting and clang-tidy diagnostics can change between
major releases, so another major is useful development feedback but is not an
equivalent acceptance result. Ordinary macOS builds retain Apple Clang by
leaving `CC` and `CXX` unset. The `full` profile selects Homebrew LLVM 21 for
sanitizer and fuzz acceptance; `GRAPHX_SANITIZER_CC`,
`GRAPHX_SANITIZER_CXX`, `GRAPHX_FUZZ_CC`, and `GRAPHX_FUZZ_CXX` may provide
explicit paths when Homebrew is installed in a nonstandard location.

Homebrew LLVM 21 AddressSanitizer has an [upstream runtime initialization hang
on macOS 26](https://github.com/llvm/llvm-project/issues/200447). On that release, the `sanitizers`, `fuzz`, and `full` profiles and the direct
`run-fuzz.sh` entry point report the limitation and use LLVM 21 UBSan for local
sanitizer instrumentation. Linux and the supported macOS 15 CI runner
retain LLVM 21 ASan+UBSan, so ASan remains a required acceptance gate. Set
`GRAPHX_SANITIZERS=address,undefined` only to retest macOS 26 after its LLVM
runtime is fixed; affected runtimes will hang before GraphX `main()` starts.

On Linux distributions that install version-suffixed tools, the defaults work
without overrides. If the locations differ, the same variables may contain
absolute paths:

```sh
export CLANG_FORMAT=/usr/bin/clang-format-21
export CLANG_TIDY=/usr/bin/clang-tidy-21
export CC=/usr/bin/clang-21
export CXX=/usr/bin/clang++-21
```

### Common overrides

Set overrides in the same shell before invoking `scripts/verify.sh`. Paths
should be absolute when the command may start Docker builds or child scripts.

| Variable | Purpose | Default |
|---|---|---|
| `CLANG_FORMAT` | LLVM 21 formatter executable | `clang-format-21` |
| `CLANG_TIDY` | LLVM 21 static analyzer executable | `clang-tidy-21` |
| `CPPCHECK` | cppcheck executable | `cppcheck` |
| `CC`, `CXX` | Optional compilers for ordinary builds; leave unset on macOS to use Apple Clang | auto-selected/toolchain default |
| `GRAPHX_SANITIZER_CC`, `GRAPHX_SANITIZER_CXX` | LLVM 21 compiler pair for sanitizer acceptance | `clang-21` on Linux; Homebrew `llvm@21` on macOS |
| `GRAPHX_FUZZ_CC`, `GRAPHX_FUZZ_CXX` | LLVM 21 compiler pair for fuzzing; takes precedence over `CC`/`CXX` | `clang-21`/`clang++-21` |
| `GRAPHX_BUILD_JOBS` | Maximum parallel build jobs | `4` |
| `GRAPHX_BUILD_DIR` | C++23 portable/native build directory | `build/dev` |
| `GRAPHX_CXX20_BUILD_DIR` | C++20 portable build directory | `build/cxx20-features` |
| `GRAPHX_QUALITY_BUILD_DIR` | Static-analysis build directory | `build/quality` |
| `GRAPHX_FUZZ_BUILD_DIR` | Fuzzer build directory | `build/fuzz` |
| `GRAPHX_FUZZ_SECONDS` | Seconds per fuzz target | `30` through `verify.sh full` |
| `GRAPHX_SANITIZERS` | Compiler sanitizer set | `address,undefined`; `undefined` automatically on macOS 26 |
| `GRAPHX_TEST_HTTP_PORT` | Portable telemetry HTTP port | `18080` |
| `GRAPHX_DOCKER_TEST_HTTP_PORT` | Published telemetry port used by Docker acceptance | `28080` |
| `GRAPHX_PHASE7_HTTP_PORT` | Published telemetry port used by the isolated history test | `38080` |
| `GRAPHX_TEST_UDP_PORT` | Portable telemetry UDP port | `19000` |
| `GRAPHX_VERIFY_LOG_DIR` | Persistent verification-log directory | `outputs/verification` |
| `GRAPHX_CA_CERT` | Public organization CA used by host npm and participating Docker builds | unset |
| `GRAPHX_CERT_INSTALL_SCRIPT` | Reviewed noninteractive Docker build certificate installer | unset |
| `ASAN_OPTIONS`, `UBSAN_OPTIONS` | Sanitizer runtime options | platform-safe profile defaults |
| `GRAPHX_ALLOW_PRIVILEGED_TESTS` | Explicit native-network authorization; must equal `1` | unset |

Example launchers also accept narrower runtime overrides. Common ones are
`GRAPHX_BUILD_DIR`, `GRAPHX_MAX_MESSAGES`, `GRAPHX_INTERVAL_MS`, and
`GRAPHX_START_DELAY_MS` for native process examples;
`GRAPHX_CAPTURE_DIR` for the capture example; and `GRAPHX_QEMU_GUI_PORT` plus
`--accel auto|kvm|tcg|hvf`, `--no-capture`, or `--no-history` for QEMU.
The SDR profiles accept `GRAPHX_SDR_GUI_PORT`, `--no-capture`, and
`--no-history`; the native profile also accepts `GRAPHX_BIN` when the GraphX CLI
is outside a detected build directory.
Application configuration can be selected with `GRAPHX_CONFIG` and scalar
configuration values with `GRAPHX_OVERRIDES`. These are deployment inputs, not
acceptance shortcuts. Unset manually exported `GRAPHX_CONFIG`,
`GRAPHX_OVERRIDES`, credentials, and topology-specific variables before moving
between unrelated examples. The portable suite isolates its own subprocesses
from these values, but an individual command-line validation intentionally
honors them. See [`README.md`](../README.md#configuration-reference) and each example's
README for its supported inputs.

For example, this selects the Homebrew LLVM 21 tools, avoids occupied portable-
test ports, uses separate build directories, and retains logs outside the
default path:

```sh
graphx_llvm21=$(brew --prefix llvm@21)
CLANG_FORMAT="$graphx_llvm21/bin/clang-format" \
CLANG_TIDY="$graphx_llvm21/bin/clang-tidy" \
GRAPHX_SANITIZER_CC="$graphx_llvm21/bin/clang" \
GRAPHX_SANITIZER_CXX="$graphx_llvm21/bin/clang++" \
GRAPHX_FUZZ_CC="$graphx_llvm21/bin/clang" \
GRAPHX_FUZZ_CXX="$graphx_llvm21/bin/clang++" \
GRAPHX_TEST_HTTP_PORT=28080 \
GRAPHX_TEST_UDP_PORT=29000 \
GRAPHX_QUALITY_BUILD_DIR="$PWD/build/quality-macos" \
GRAPHX_FUZZ_BUILD_DIR="$PWD/build/fuzz-macos" \
GRAPHX_VERIFY_LOG_DIR="$PWD/outputs/verification-macos" \
  scripts/verify.sh full
```

## Run the tests

For an ordinary code edit, start here:

```sh
scripts/verify.sh quick
```

Before handing a change to another implementer or verifier, run:

```sh
scripts/verify.sh portable
```

Before a pull request or broad acceptance decision, first confirm the selected
container engine is ready, then run:

```sh
docker context show
docker info
docker compose version
scripts/verify.sh full
```

On a dedicated native Linux test host, review the network operations in
[`test-reference.md`](test-reference.md#6-native-linux-network-drivers), then run
as the normal login user:

```sh
GRAPHX_ALLOW_PRIVILEGED_TESTS=1 scripts/verify.sh native-linux
```

The example scripts request sudo only for the operations that require it. Do not
run the entire verification command as root. The profile requires dumpcap and
tshark so a successful result includes live-capture and dissector
evidence. Teardown helpers are safe to run twice.

For a clean release commit:

```sh
scripts/verify.sh release
```

The release profile creates uniquely named build and output directories. It
does not publish anything and does not permit the development-only
`--allow-dirty` override.

## Focused automated tests

Use a focused command while diagnosing a failure, then rerun the applicable
profile before recording acceptance:

```sh
ctest --test-dir build/dev -N
ctest --test-dir build/dev -R '<test-name>' --output-on-failure
npm test --prefix apps/telemetry
npm test --prefix web
```

The profiles exercise the portable process pipelines, checked-in
configurations, telemetry service, console, and Docker acceptance appropriate
to the selected profile. They do not turn a dry-run into OVS, packet-delivery,
capture, QEMU, or KVM evidence. Runnable workflows belong in
[`demo-guide.md`](demo-guide.md); platform-specific interactive evidence and
cleanup belong in [`manual-test-procedures.md`](manual-test-procedures.md);
exact low-level expectations remain in [`test-reference.md`](test-reference.md).

## Independent verification

An independent verifier should test the exact candidate commit, record the host
and tool versions, and use new build directories. For portable verification:

```sh
run_id=$(date -u +%Y%m%dT%H%M%SZ)
GRAPHX_BUILD_DIR="$PWD/build/verify-$run_id-cxx23" \
GRAPHX_CXX20_BUILD_DIR="$PWD/build/verify-$run_id-cxx20" \
  scripts/verify.sh portable
```

Record the final `PASS` or `FAIL`, the log path printed by the wrapper, and any
explicitly unavailable platform gate. Code inspection and container results
must not be reported as native runtime verification.

## Organization certificates

To add organization trust to host npm and all Docker builds used by the
profiles, export an absolute path to a public root CA. A reviewed installer may
also configure the Docker build stages:

```sh
export GRAPHX_CA_CERT=/absolute/path/to/company-root-ca.crt
export GRAPHX_CERT_INSTALL_SCRIPT=/absolute/path/to/install-certs.sh
scripts/verify.sh full
```

Host-side `npm ci` uses the operating-system trust store and, when set,
`GRAPHX_CA_CERT` as additional trust without replacing Node.js's public roots.
This recognizes an organization CA that its installer has already added to the
macOS Keychain or Linux system trust store. `GRAPHX_CERT_INSTALL_SCRIPT` is not
run on the host; it runs noninteractively in the image trust-bootstrap stage,
where the resulting CA bundle is configured for Docker-based `npm ci`. The
installer must not use sudo and must not contain private keys, registry
passwords, or npm tokens. See the
[`certificate bootstrap reference`](test-reference.md#local-linux-verifier-container-on-macos)
for the complete trust model.

## If a test fails

1. Read the final failing gate and log path printed by `scripts/verify.sh`.
2. Fix the first substantive failure before interpreting later missing results.
3. For Docker failures, inspect service status and logs, then use the matching
   teardown command.
4. For an interrupted native network run, use the example's canonical
   `scripts/down.sh`, or its explicitly named `down-native-linux.sh` helper,
   before retrying.
5. Preserve the log with the verification report.

Focused reruns, expected results, manual telemetry checks, capture diagnostics,
native cleanup, and every acceptance criterion remain in
[`test-reference.md`](test-reference.md). Release policy is documented in
[`release-process.md`](release-process.md).
