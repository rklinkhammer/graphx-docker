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

- `full`: Docker Compose, Clang 18, clang-format-18, clang-tidy-18, cppcheck,
  `xxd`, and libFuzzer support.
- `native-linux`: Docker Compose, Open vSwitch, iproute2, nftables, dumpcap,
  tshark, and sudo access.
- `release`: Python 3 and a clean Git worktree at the intended release commit.

The commands stop at the first failure and write a combined log under
`outputs/verification/`. Set `GRAPHX_VERIFY_LOG_DIR` to use another location.
Portable tests isolate telemetry and web subprocesses from inherited `GRAPHX_*`
deployment variables, so container-only secret paths cannot affect host tests.

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
