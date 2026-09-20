# Four-radio VITA development

[Implementation plan](implementation-plan.md) · [Accepted brief](../../docs/vita_system.md)

The opt-in standalone radio uses `vrt_framework` as its only VITA codec and
protocol runtime, with one virtual SoapySDR device per radio process. The reusable
`ControllerSession` uses the same runtime and authenticated transport boundary.
The opt-in `graphx-vita-processor` and `graphx-vita-detector` implement P2. P3 adds authoritative jumbo attachments and the separate Linux
`graphx-vita-recorder`; privileged OVS acceptance is pending. Images and the
complete graph remain P5 work. These targets are not part of published native/OCI releases.

## Build and test

Use a C++23 compiler/standard library, CMake, Ninja, Git, OpenSSL 3 and Python 3.
Apple clang 21 is the tested macOS toolchain. From the repository root:

```sh
cmake -S . -B build/vita-migration -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DGRAPHX_BUILD_TESTS=ON -DGRAPHX_BUILD_VITA_RADIO=ON \
  -DGRAPHX_VRT_REPOSITORY=/Users/rklinkhammer/workspace/vrt_framework
cmake --build build/vita-migration -j4
ctest --test-dir build/vita-migration -R graphx-vita --output-on-failure
```

`GRAPHX_VRT_REPOSITORY` selects a Git mirror, not a revision override. CMake checks
out and verifies the clean pinned commit
`dbe85d37155145842da60367af1c4beef8801b0c`. Omit the repository argument when that
commit is available from the default GitHub repository. At migration verification
it was available locally; the GitHub archive endpoint returned 404. No library
push is performed by the build. SoapySDR uses its SHA-256-checked source archive.
No Python packet generator or generated VITA classes are required.

The build emits `generated/vita-dependencies.json`, upstream texts under
`generated/vita-licenses/`, and `generated/vita-dependencies.spdx.json`. The SPDX
file records dependency inputs; release packaging must still inventory the
complete linked application, OpenSSL, system libraries and artifacts.

To inspect cross-process results directly:

```sh
python3 tests/test_vita_radio.py build/vita-migration
python3 tests/test_vita_controller.py build/vita-migration
python3 tests/test_vita_processing.py build/vita-migration
python3 tests/test_vita_processing.py build/vita-migration --boundary
python3 tests/test_vita_processing.py build/vita-migration --missing
```

The independent Python wire harness starts four owned native processes, normalizes
private authored graph fixtures through the authoritative loader, and stages
short-lived mTLS credentials. It pauses/resumes only those processes to test
activation jitter. It independently checks UDP and decrypted control bytes,
including a common scheduled epoch, rational timestamps and replay expiration.
The retention-expiration case deliberately waits 31 seconds. The bounded TLS BIO stress harness checks output backpressure, partial writes,
deadline expiry and lease/completion cleanup. The C++ controller
harness exercises the production `ControllerSession` APIs over actual mTLS.
Neither harness creates Docker/OVS infrastructure or runs privileged operations.

The radio uses the existing arguments:
`--node ID --config NORMALIZED_NODE_JSON --release-file FILE --release-token TOKEN`.
`GRAPHX_CREDENTIALS` selects staged credentials; managed telemetry retains its
existing credential and release-barrier contract. Use the harness to construct
valid normalized fixtures rather than bypassing the loader or startup barrier.

See [radio design](radio-design.md), [command contract](command-analysis.md),
[epoch regression](migration-blocker.md) and [P1 verification](p1-verification.md),
[processing design](processing-design.md), [P2 verification](p2-verification.md),
[network design](network-design.md), [P3 verification](p3-verification.md),
[lifecycle design](lifecycle-design.md) and [P4 verification](p4-verification.md).

For pending live acceptance, start with the [P3/P4 operator runbook](privileged-verification-runbook.md): preparation, Mac-side preflight, authorization, case matrices and owned recovery.
