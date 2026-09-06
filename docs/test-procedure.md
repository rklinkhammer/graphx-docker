# GraphX test procedure

This is the short testing entry point for developers and independent verifiers.
Choose one profile, run one command, and retain the generated log. Detailed
coverage, rationale, and manual diagnostics are in
[`test-reference.md`](test-reference.md).

## Choose a profile

| Profile | Use it when | Coverage | Host |
|---|---|---|---|
| `quick` | Editing C++ code | Fresh development build and CTest | macOS or Linux |
| `portable` | Preparing a normal change | C++20/23, configurations, process pipelines, telemetry, web, and portable examples | macOS or Linux |
| `full` | Preparing a pull request | Formatting, static analysis, sanitizers, fuzzing, portable acceptance, and Docker acceptance | macOS or Linux with Docker |
| `native-linux` | Certifying network behavior | Portable acceptance plus UDP broadcast, macvlan, IPvlan, OVS, namespaces, nftables, netem, and live packet capture | Native Linux only |
| `release` | Building a release candidate | Clean release build, package contract, SBOM, checksums, and independent artifact verification | Supported release host |

`native-linux` is the only profile that validates native Linux network-driver
behavior. A Linux container or Docker Desktop VM is useful evidence but is not a
substitute for that profile.

## Prerequisites

All profiles need CMake 3.25+, Ninja, OpenSSL 3 development files, and a C++20/23
compiler. Portable testing also needs Node.js, npm, and curl.

Additional requirements:

- `full`: Docker Compose, LLVM/Clang 21 with its sanitizer and libFuzzer
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

### macOS LLVM 21 setup

Homebrew's `llvm@21` formula is keg-only. It supplies `clang-format` and
`clang-tidy` beneath its own prefix, not commands named `clang-format-21` and
`clang-tidy-21`. Install and select the pinned tools explicitly:

```sh
brew install llvm@21 cppcheck

graphx_llvm21=$(brew --prefix llvm@21)
export CLANG_FORMAT="$graphx_llvm21/bin/clang-format"
export CLANG_TIDY="$graphx_llvm21/bin/clang-tidy"
export GRAPHX_FUZZ_CC="$graphx_llvm21/bin/clang"
export GRAPHX_FUZZ_CXX="$graphx_llvm21/bin/clang++"
unset CC CXX
```

Confirm that the selected formatter and analyzer both report version 21 before
running `full`:

```sh
"$CLANG_FORMAT" --version
"$CLANG_TIDY" --version
/usr/bin/c++ --version
scripts/verify.sh full
```

Do not point these variables at Homebrew's current unversioned `llvm` formula
unless it is LLVM 21. Formatting and clang-tidy diagnostics can change between
major releases, so another major is useful development feedback but is not an
equivalent acceptance result. Ordinary macOS builds retain Apple Clang by
leaving `CC` and `CXX` unset. The `full` profile selects Homebrew LLVM 21 for
sanitizer and fuzz acceptance; `GRAPHX_SANITIZER_CC`,
`GRAPHX_SANITIZER_CXX`, `GRAPHX_FUZZ_CC`, and `GRAPHX_FUZZ_CXX` may provide
explicit paths when Homebrew is installed in a nonstandard location.

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
| `GRAPHX_TEST_HTTP_PORT` | Portable telemetry HTTP port | `18080` |
| `GRAPHX_TEST_UDP_PORT` | Portable telemetry UDP port | `19000` |
| `GRAPHX_VERIFY_LOG_DIR` | Persistent verification-log directory | `outputs/verification` |
| `GRAPHX_CA_CERT` | Public organization CA used by all participating Docker builds | unset |
| `GRAPHX_CERT_INSTALL_SCRIPT` | Reviewed noninteractive certificate installer | unset |
| `ASAN_OPTIONS`, `UBSAN_OPTIONS` | Sanitizer runtime options | platform-safe profile defaults |
| `GRAPHX_ALLOW_PRIVILEGED_TESTS` | Explicit native-network authorization; must equal `1` | unset |

Example launchers also accept narrower runtime overrides. Common ones are
`GRAPHX_BUILD_DIR`, `GRAPHX_MAX_MESSAGES`, `GRAPHX_INTERVAL_MS`, and
`GRAPHX_START_DELAY_MS` for native process examples;
`GRAPHX_CAPTURE_DIR` for the capture example; and `GRAPHX_QEMU_GUI_PORT` plus
`--accel auto|kvm|tcg|hvf`, `--no-capture`, or `--no-history` for QEMU.
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

Before a pull request or broad acceptance decision, run:

```sh
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
tshark so a successful result includes Phase 11 live-capture and dissector
evidence. Teardown helpers are safe to run twice.

For a clean release commit:

```sh
scripts/verify.sh release
```

The release profile creates uniquely named build and output directories. It
does not publish anything and does not permit the development-only
`--allow-dirty` override.

## Focused tests and examples

Use a focused command while diagnosing a failure, then rerun the applicable
profile before recording acceptance. The profile scripts already exercise the
standard bridge demo, shared-memory, UDP unicast/multicast, and—in Docker mode—
the isolated UDP broadcast example. Interactive examples remain valuable for
GUI, capture, accelerator, and platform-specific evidence.

| Area | Focused command | Platform and reference |
|---|---|---|
| One CTest | `ctest --test-dir build/dev -R '<test-name>' --output-on-failure` | macOS/Linux; list names with `ctest --test-dir build/dev -N` |
| Standard GUI demo | `scripts/demo.sh start`, then `scripts/demo.sh verify` and `scripts/demo.sh stop` | macOS/Linux; [`complete-system-demo.md`](complete-system-demo.md) |
| Shared memory | `GRAPHX_BUILD_DIR="$PWD/build/dev" examples/shared-memory/run.sh` | macOS/Linux; [`shared-memory`](../examples/shared-memory/README.md) |
| UDP unicast | `GRAPHX_BUILD_DIR="$PWD/build/dev" examples/udp-unicast/run.sh` | macOS/Linux; [`UDP guide`](udp-transport.md) |
| UDP multicast | `GRAPHX_BUILD_DIR="$PWD/build/dev" examples/udp-multicast/run.sh` | macOS/Linux; [`UDP guide`](udp-transport.md) |
| UDP broadcast | `examples/udp-broadcast/run.sh` | Docker on macOS/Linux; [`broadcast example`](../examples/udp-broadcast/README.md) |
| Native UDP broadcast | `GRAPHX_VERIFY_LIVE_CAPTURE=1 examples/udp-broadcast/run-native-linux.sh` | Native Linux only; use `down-native-linux.sh` afterward |
| External QEMU | `examples/qemu-node/external/scripts/demo.sh start --accel auto`, then `verify` and `stop` | macOS/Linux; [`QEMU guide`](qemu-demos.md) |
| Container QEMU | `examples/qemu-node/container/scripts/demo.sh start --accel kvm` | Native Linux, operator-run; [`QEMU guide`](qemu-demos.md) |
| macOS OVS simulation | `examples/mixed-network/scripts/macos-up.sh`, then `macos-down.sh` | Docker Desktop; [`network guide`](network-infrastructure.md) |
| Native network labs | `examples/<lab>/scripts/up.sh`, where `<lab>` is `macvlan`, `ipvlan-l2`, or `ipvlan-l3` | Native Linux only; always use the matching `down.sh` |
| Mixed native network | `examples/mixed-network/scripts/linux-up.sh` | Native Linux only; use `linux-down.sh` afterward |

For the browser topology, control tokens, history, and capture workflow across
the graphical examples, follow
[`graphical-examples-guide.md`](graphical-examples-guide.md). For exact expected
results, negative tests, Linux cleanup, and packet-capture diagnostics, use
[`test-reference.md`](test-reference.md).

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

To add organization trust to all Docker builds used by the profiles, export an
absolute path to a public root CA, a reviewed installer, or both:

```sh
export GRAPHX_CA_CERT=/absolute/path/to/company-root-ca.crt
export GRAPHX_CERT_INSTALL_SCRIPT=/absolute/path/to/install-certs.sh
scripts/verify.sh full
```

The installer runs noninteractively in the image trust-bootstrap stage. It must
not use sudo and must not contain private keys, registry passwords, or npm
tokens. See the
[`certificate bootstrap reference`](test-reference.md#local-linux-verifier-container-on-macos)
for the complete trust model.

## If a test fails

1. Read the final failing gate and log path printed by `scripts/verify.sh`.
2. Fix the first substantive failure before interpreting later missing results.
3. For Docker failures, inspect service status and logs, then use the matching
   teardown command.
4. For an interrupted native network run, use the example's `down.sh`,
   `linux-down.sh`, or `down-native-linux.sh` before retrying.
5. Preserve the log with the verification report.

Focused reruns, expected results, manual telemetry checks, capture diagnostics,
native cleanup, and every acceptance criterion remain in
[`test-reference.md`](test-reference.md). Release policy is documented in
[`release-process.md`](release-process.md).
